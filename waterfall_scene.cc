#include <algorithm>
#include <cmath>
#include <QPixmap>
#include <QMenu>
#include <QClipboard>
#include <QApplication>
#include <QGraphicsSceneMouseEvent>
#include <QGraphicsView>

#include "waterfall_item.h"
#include "waterfall_scene.h"

waterfall_scene::waterfall_scene(QObject* parent)
    : QGraphicsScene(parent), current_col_width_(kMinColWidth), last_layout_item_index_(0), current_session_id_(0)
{
}

void waterfall_scene::clear_items()
{
    emit request_cancel_all();
    current_session_id_++;
    items_.clear();
    item_map_.clear();
    loaded_items_.clear();
    col_heights_.clear();
    this->clear();
    last_layout_item_index_ = 0;
    request_counter_ = 0;
    setSceneRect(0, 0, 0, 0);
}

void waterfall_scene::add_image(const image_meta& meta)
{
    auto* item = new waterfall_item(meta);
    addItem(item);
    items_.push_back(item);
    item_map_.insert(meta.path, item);

    double ratio = 1.0;
    if (meta.original_size.isValid() && meta.original_size.width() > 0)
    {
        ratio = static_cast<double>(meta.original_size.height()) / meta.original_size.width();
    }

    int thumb_height = static_cast<int>(kMinColWidth * ratio);

    QPixmap placeholder(kMinColWidth, thumb_height);
    placeholder.fill(QColor(230, 230, 230));
    item->setPixmap(placeholder);
}

void waterfall_scene::layout_items(int view_width)
{
    if (items_.empty() || view_width <= 0)
    {
        return;
    }

    int available_width = view_width - (2 * kItemMargin);
    int col_count = std::max(1, available_width / (kMinColWidth + kColumnMargin));
    int total_spacing = (col_count - 1) * kColumnMargin;
    int real_col_width = (available_width - total_spacing) / col_count;

    if (real_col_width != current_col_width_ || col_heights_.size() != static_cast<size_t>(col_count))
    {
        current_col_width_ = real_col_width;
        col_heights_.assign(col_count, kItemMargin);
        last_layout_item_index_ = 0;
    }

    for (size_t i = last_layout_item_index_; i < items_.size(); ++i)
    {
        waterfall_item* item = items_[i];
        auto min_itr = std::min_element(col_heights_.begin(), col_heights_.end());
        int min_col_idx = static_cast<int>(std::distance(col_heights_.begin(), min_itr));

        int x = kItemMargin + (min_col_idx * (real_col_width + kColumnMargin));
        int y = *min_itr;

        item->setPos(x, y);
        item->set_display_width(real_col_width);

        int item_height = static_cast<int>(item->sceneBoundingRect().height());
        col_heights_[min_col_idx] += (item_height + kItemMargin);
    }

    last_layout_item_index_ = items_.size();
    int max_height = *std::max_element(col_heights_.begin(), col_heights_.end());
    setSceneRect(0, 0, view_width, max_height + 50);
}

void waterfall_scene::load_visible_items(const QRectF& visible_rect)
{
    QRectF load_rect = visible_rect.adjusted(0, -800, 0, 1200);
    QRectF unload_rect = visible_rect.adjusted(0, -2500, 0, 3000);

    qreal dpr = 1.0;
    if (!views().isEmpty())
    {
        if (auto* v = views().first())
        {
            dpr = v->devicePixelRatio();
        }
    }

    QList<load_task> tasks_to_load;
    QList<QString> paths_to_cancel;

    QList<QGraphicsItem*> visible_graphics_items = items(load_rect);

    for (auto* g_item : visible_graphics_items)
    {
        auto* item = dynamic_cast<waterfall_item*>(g_item);
        if (item == nullptr)
        {
            continue;
        }

        if (item->is_loaded() || item->wants_loading())
        {
            continue;
        }

        item->set_wants_loading(true);
        item->set_loading(true);

        int req_width = static_cast<int>(current_col_width_ * dpr);
        int req_height = 0;

        QSize orig_size = item->get_original_size();
        if (orig_size.isValid() && orig_size.width() > 0)
        {
            double ratio = static_cast<double>(orig_size.height()) / orig_size.width();
            req_height = static_cast<int>(req_width * ratio);
        }
        else if (!item->pixmap().isNull() && item->pixmap().width() > 0)
        {
            double ratio = static_cast<double>(item->pixmap().height()) / item->pixmap().width();
            req_height = static_cast<int>(req_width * ratio);
        }

        QSize target_size(req_width, req_height);
        quint64 request_id = ++request_counter_;
        item->set_request_id(request_id);

        tasks_to_load.append({request_id, item->get_path(), target_size, current_session_id_});
    }

    auto it = loaded_items_.begin();
    while (it != loaded_items_.end())
    {
        waterfall_item* item = *it;
        if (item == nullptr)
        {
            it = loaded_items_.erase(it);
            continue;
        }

        if (!unload_rect.intersects(item->sceneBoundingRect()))
        {
            item->set_wants_loading(false);
            if (item->is_loading())
            {
                paths_to_cancel.append(item->get_path());
            }
            item->unload();
            it = loaded_items_.erase(it);
        }
        else
        {
            ++it;
        }
    }

    if (!tasks_to_load.isEmpty())
    {
        emit request_load_batch(tasks_to_load);
    }

    if (!paths_to_cancel.isEmpty())
    {
        emit request_cancel_batch(paths_to_cancel);
    }
}

void waterfall_scene::on_image_loaded(quint64 id, const QString& path, const QImage& image, int session_id)
{
    if (session_id != current_session_id_)
    {
        return;
    }

    auto it = item_map_.find(path);
    if (it == item_map_.end())
    {
        return;
    }

    waterfall_item* item = it.value();

    if (item->get_request_id() != id)
    {
        return;
    }

    if (!item->wants_loading())
    {
        return;
    }

    item->set_pixmap_safe(QPixmap::fromImage(image));

    item->set_display_width(current_col_width_);
    item->set_loaded(true);
    item->set_loading(false);
    loaded_items_.insert(item);
}

std::vector<QString> waterfall_scene::get_all_paths() const
{
    std::vector<QString> paths;
    paths.reserve(items_.size());
    for (const auto* item : items_)
    {
        paths.push_back(item->get_path());
    }
    return paths;
}

void waterfall_scene::mouseDoubleClickEvent(QGraphicsSceneMouseEvent* event)
{
    QGraphicsItem* item = itemAt(event->scenePos(), QTransform());
    auto* wf_item = dynamic_cast<waterfall_item*>(item);

    if (wf_item != nullptr)
    {
        emit image_double_clicked(wf_item->get_path());
    }
    QGraphicsScene::mouseDoubleClickEvent(event);
}

void waterfall_scene::contextMenuEvent(QGraphicsSceneContextMenuEvent* event)
{
    QGraphicsItem* item = itemAt(event->scenePos(), QTransform());
    auto* wf_item = dynamic_cast<waterfall_item*>(item);
    QMenu menu;

    if (wf_item != nullptr)
    {
        QAction* copyPathAction = menu.addAction("复制文件绝对路径");
        connect(copyPathAction,
                &QAction::triggered,
                [wf_item]()
                {
                    QClipboard* clipboard = QGuiApplication::clipboard();
                    clipboard->setText(wf_item->get_path());
                });
        menu.addSeparator();
    }
    QAction* openAction = menu.addAction("打开文件夹");
    connect(openAction, &QAction::triggered, this, &waterfall_scene::request_open_folder);
    menu.exec(event->screenPos());
    event->accept();
}
