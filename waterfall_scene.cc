#include <algorithm>
#include <cmath>
#include <QPixmap>
#include <QMenu>
#include <QClipboard>
#include <QApplication>
#include <QGraphicsSceneMouseEvent>
#include <QGraphicsView>
#include <QDebug>
#include <QtConcurrent>
#include "common_types.h"
#include "waterfall_scene.h"
#include "waterfall_item.h"

static layout_result calculate_layout_job(const std::vector<QSize>& sizes, int view_width, int kItemMargin, int kColumnMargin, int kMinColWidth)
{
    layout_result result;
    result.view_width = view_width;
    result.count = sizes.size();

    if (sizes.empty() || view_width <= 0)
    {
        return result;
    }

    int available_width = view_width - (2 * kItemMargin);
    int col_count = std::max(1, available_width / (kMinColWidth + kColumnMargin));
    int total_spacing = (col_count - 1) * kColumnMargin;
    int real_col_width = (available_width - total_spacing) / col_count;

    result.col_heights.assign(col_count, kItemMargin);
    result.rects.reserve(sizes.size());
    result.item_y_index.reserve(sizes.size());

    for (const auto& original_size : sizes)
    {
        auto min_itr = std::min_element(result.col_heights.begin(), result.col_heights.end());
        int min_col_idx = static_cast<int>(std::distance(result.col_heights.begin(), min_itr));

        int x = kItemMargin + (min_col_idx * (real_col_width + kColumnMargin));
        int y = *min_itr;

        double ratio = 1.0;
        if (original_size.isValid() && original_size.width() > 0)
        {
            ratio = static_cast<double>(original_size.height()) / original_size.width();
        }
        int h = static_cast<int>(real_col_width * ratio);

        result.rects.emplace_back(x, y, real_col_width, h);
        result.item_y_index.push_back(y);

        result.col_heights[min_col_idx] += (h + kItemMargin);
    }

    result.max_height = *std::max_element(result.col_heights.begin(), result.col_heights.end());
    return result;
}

waterfall_scene::waterfall_scene(QObject* parent) : QGraphicsScene(parent)
{
    connect(&layout_watcher_, &QFutureWatcher<layout_result>::finished, this, &waterfall_scene::on_layout_finished);

    for (int i = 0; i < 20; ++i)
    {
        auto* item = new waterfall_item();
        addItem(item);
        item->setVisible(false);
        pool_.push(item);
    }
}

waterfall_scene::~waterfall_scene()
{
    if (layout_watcher_.isRunning())
    {
        layout_watcher_.cancel();
        layout_watcher_.waitForFinished();
    }
}

void waterfall_scene::clear_items()
{
    if (layout_watcher_.isRunning())
    {
        layout_watcher_.disconnect();

        connect(&layout_watcher_, &QFutureWatcher<layout_result>::finished, this, &waterfall_scene::on_layout_finished);
    }

    emit request_cancel_all();
    current_session_id_++;

    auto keys = active_items_.keys();
    for (int idx : keys)
    {
        recycle_item(active_items_.take(idx));
    }

    all_models_.clear();
    item_y_index_.clear();
    col_heights_.clear();
    last_layout_index_ = 0;
    request_counter_ = 0;
    pending_view_width_ = 0;

    setSceneRect(0, 0, 0, 0);
}

void waterfall_scene::add_images(const QList<image_meta>& batch)
{
    int start_index = static_cast<int>(all_models_.size());
    all_models_.reserve(all_models_.size() + batch.size());
    item_y_index_.reserve(all_models_.size() + batch.size());

    for (int i = 0; i < batch.size(); ++i)
    {
        layout_model model;
        model.index = start_index + i;
        model.path = batch[i].path;
        model.original_size = batch[i].original_size;

        all_models_.push_back(model);
        item_y_index_.push_back(0);
    }
}

void waterfall_scene::layout_models(int view_width)
{
    if (all_models_.empty() || view_width <= 0)
    {
        return;
    }

    int available_width = view_width - (2 * kItemMargin);
    int col_count = std::max(1, available_width / (kMinColWidth + kColumnMargin));
    int total_spacing = (col_count - 1) * kColumnMargin;
    int real_col_width = (available_width - total_spacing) / col_count;

    bool need_full_relayout = (real_col_width != current_col_width_) || (col_heights_.size() != static_cast<size_t>(col_count));

    if (!need_full_relayout)
    {
        if (last_layout_index_ < all_models_.size())
        {
            if (col_heights_.empty())
            {
                col_heights_.assign(col_count, kItemMargin);
            }

            for (size_t i = last_layout_index_; i < all_models_.size(); ++i)
            {
                layout_model& model = all_models_[i];

                auto min_itr = std::min_element(col_heights_.begin(), col_heights_.end());
                int min_col_idx = static_cast<int>(std::distance(col_heights_.begin(), min_itr));

                int x = kItemMargin + (min_col_idx * (real_col_width + kColumnMargin));
                int y = *min_itr;

                double ratio = 1.0;
                if (model.original_size.isValid() && model.original_size.width() > 0)
                {
                    ratio = static_cast<double>(model.original_size.height()) / model.original_size.width();
                }
                int h = static_cast<int>(real_col_width * ratio);

                model.layout_rect = QRectF(x, y, real_col_width, h);
                item_y_index_[i] = y;

                col_heights_[min_col_idx] += (h + kItemMargin);
            }

            last_layout_index_ = all_models_.size();
            int max_height = *std::max_element(col_heights_.begin(), col_heights_.end());

            setSceneRect(0, 0, view_width, max_height + 50);

            if (!views().isEmpty())
            {
                update_viewport(views().first()->mapToScene(views().first()->viewport()->rect()).boundingRect());
            }
        }
        return;
    }

    if (layout_watcher_.isRunning())
    {
        pending_view_width_ = view_width;
        return;
    }

    std::vector<QSize> sizes;
    sizes.reserve(all_models_.size());
    for (const auto& m : all_models_)
    {
        sizes.push_back(m.original_size);
    }

    QFuture<layout_result> future = QtConcurrent::run(calculate_layout_job, sizes, view_width, kItemMargin, kColumnMargin, kMinColWidth);

    layout_watcher_.setFuture(future);
}

void waterfall_scene::on_layout_finished()
{
    layout_result result = layout_watcher_.result();

    if (pending_view_width_ > 0 && pending_view_width_ != result.view_width)
    {
        int next_width = pending_view_width_;
        pending_view_width_ = 0;

        layout_models(next_width);
        return;
    }

    size_t count = std::min(result.count, all_models_.size());

    for (size_t i = 0; i < count; ++i)
    {
        all_models_[i].layout_rect = result.rects[i];
        item_y_index_[i] = result.item_y_index[i];
    }

    col_heights_ = result.col_heights;

    current_col_width_ = static_cast<int>(result.rects.empty() ? kMinColWidth : (result.rects[0].width()));
    last_layout_index_ = count;

    if (count < all_models_.size())
    {
        layout_models(result.view_width);
    }
    else
    {
        setSceneRect(0, 0, result.view_width, result.max_height + 50);

        if (!views().isEmpty())
        {
            QGraphicsView* v = views().first();
            QRectF visible_rect = v->mapToScene(v->viewport()->rect()).boundingRect();

            for (auto it = active_items_.begin(); it != active_items_.end(); ++it)
            {
                waterfall_item* item = it.value();
                int idx = it.key();

                if (idx < static_cast<int>(all_models_.size()))
                {
                    item->bind_model(all_models_[idx], current_col_width_, item->get_request_id());
                }
            }

            update_viewport(visible_rect);
        }
    }
}

void waterfall_scene::update_viewport(const QRectF& visible_rect)
{
    if (all_models_.empty())
    {
        return;
    }

    QRectF load_rect = visible_rect.adjusted(0, -1000, 0, 1500);
    int top_y = static_cast<int>(load_rect.top());
    int bottom_y = static_cast<int>(load_rect.bottom());

    auto it_start = std::lower_bound(item_y_index_.begin(), item_y_index_.end(), top_y);
    int start_idx = static_cast<int>(std::distance(item_y_index_.begin(), it_start));
    start_idx = std::max(0, start_idx - 20);

    int end_idx = start_idx;
    for (; end_idx < static_cast<int>(all_models_.size()); ++end_idx)
    {
        if (item_y_index_[end_idx] > bottom_y)
        {
            end_idx = std::min(static_cast<int>(all_models_.size()) - 1, end_idx + 20);
            break;
        }
    }
    if (end_idx >= static_cast<int>(all_models_.size()))
    {
        end_idx = static_cast<int>(all_models_.size()) - 1;
    }

    QSet<int> needed_indices;
    QList<load_task> tasks_to_load;
    QList<QString> paths_to_cancel;

    qreal dpr = 1.0;
    if (!views().isEmpty())
    {
        dpr = views().first()->devicePixelRatio();
    }

    for (int i = start_idx; i <= end_idx; ++i)
    {
        const auto& model = all_models_[i];
        if (!load_rect.intersects(model.layout_rect))
        {
            continue;
        }

        needed_indices.insert(i);

        if (active_items_.contains(i))
        {
            continue;
        }

        waterfall_item* item = obtain_item();
        quint64 req_id = ++request_counter_;

        item->bind_model(model, current_col_width_, req_id);
        active_items_.insert(i, item);

        int req_w = static_cast<int>(model.layout_rect.width() * dpr);
        int req_h = static_cast<int>(model.layout_rect.height() * dpr);
        tasks_to_load.append({req_id, model.path, QSize(req_w, req_h), current_session_id_});
    }

    auto current_keys = active_items_.keys();
    for (int idx : current_keys)
    {
        if (!needed_indices.contains(idx))
        {
            waterfall_item* item = active_items_.take(idx);
            paths_to_cancel.append(item->get_path());
            recycle_item(item);
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

waterfall_item* waterfall_scene::obtain_item()
{
    if (!pool_.isEmpty())
    {
        waterfall_item* item = pool_.pop();

        return item;
    }
    auto* item = new waterfall_item();
    addItem(item);
    return item;
}

void waterfall_scene::recycle_item(waterfall_item* item)
{
    if (item == nullptr)
    {
        return;
    }
    item->reset();
    pool_.push(item);
}

void waterfall_scene::on_image_loaded(quint64 id, const QString& /*path*/, const QImage& image, int session_id)
{
    if (session_id != current_session_id_)
    {
        return;
    }

    for (auto it = active_items_.begin(); it != active_items_.end(); ++it)
    {
        waterfall_item* item = it.value();
        if (item->get_request_id() == id)
        {
            item->set_pixmap_safe(QPixmap::fromImage(image));
            return;
        }
    }
}

void waterfall_scene::on_tasks_dropped(const QList<QString>& paths) {}

std::vector<QString> waterfall_scene::get_all_paths() const
{
    std::vector<QString> paths;
    paths.reserve(all_models_.size());
    for (const auto& m : all_models_)
    {
        paths.push_back(m.path);
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
        QAction* copyPathAction = menu.addAction("复制图片路径");
        connect(copyPathAction, &QAction::triggered, [wf_item]() { QGuiApplication::clipboard()->setText(wf_item->get_path()); });
        menu.addSeparator();
    }

    QAction* openAction = menu.addAction("打开新文件夹");
    connect(openAction, &QAction::triggered, this, &waterfall_scene::request_open_folder);

    menu.exec(event->screenPos());
    event->accept();
}
