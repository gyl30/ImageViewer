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

waterfall_scene::waterfall_scene(QObject* parent) : QGraphicsScene(parent), current_col_width_(kMinColWidth) {}

void waterfall_scene::clear_items()
{
    items_.clear();
    item_map_.clear();
    this->clear();

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

    current_col_width_ = real_col_width;

    std::vector<int> col_heights(col_count, kItemMargin);

    for (auto* item : items_)
    {
        auto min_itr = std::min_element(col_heights.begin(), col_heights.end());
        int min_col_idx = static_cast<int>(std::distance(col_heights.begin(), min_itr));

        int x = kItemMargin + (min_col_idx * (real_col_width + kColumnMargin));
        int y = *min_itr;

        item->setPos(x, y);

        item->set_display_width(real_col_width);

        int item_height = static_cast<int>(item->sceneBoundingRect().height());

        col_heights[min_col_idx] += (item_height + kItemMargin);
    }

    int max_height = *std::max_element(col_heights.begin(), col_heights.end());
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

    QList<QGraphicsItem*> visible_graphics_items = items(load_rect);

    for (auto* g_item : visible_graphics_items)
    {
        auto* item = dynamic_cast<waterfall_item*>(g_item);
        if (item == nullptr)
        {
            continue;
        }

        if (item->is_loaded() || item->is_loading())
        {
            continue;
        }

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

        emit request_load_image(item->get_path(), target_size);
    }

    for (auto* item : items_)
    {
        if (!item->is_loaded() && !item->is_loading())
        {
            continue;
        }

        QRectF item_rect = item->sceneBoundingRect();

        if (!unload_rect.intersects(item_rect))
        {
            if (item->is_loading())
            {
                emit request_cancel_image(item->get_path());
            }

            item->unload();
        }
    }
}
void waterfall_scene::on_image_loaded(const QString& path, const QImage& image)
{
    if (auto it = item_map_.find(path); it != item_map_.end())
    {
        waterfall_item* item = it.value();

        item->set_pixmap_safe(QPixmap::fromImage(image));
        item->set_display_width(current_col_width_);

        item->set_loaded(true);
        item->set_loading(false);
    }
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
