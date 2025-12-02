#include <algorithm>
#include <cmath>
#include <QGraphicsSceneMouseEvent>
#include <QPixmap>

#include "waterfall_item.h"
#include "waterfall_scene.h"

waterfall_scene::waterfall_scene(QObject* parent) : QGraphicsScene(parent), current_col_width_(kMinColWidth) {}

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
    QList<QGraphicsItem*> visible_items = items(visible_rect);

    for (QGraphicsItem* g_item : visible_items)
    {
        auto* item = dynamic_cast<waterfall_item*>(g_item);

        if (item != nullptr && !item->is_loaded() && !item->is_loading())
        {
            item->set_loading(true);

            int req_height = 0;
            if (!item->pixmap().isNull() && item->pixmap().width() > 0)
            {
                double ratio = static_cast<double>(item->pixmap().height()) / item->pixmap().width();
                req_height = static_cast<int>(current_col_width_ * ratio);
            }

            emit request_load_image(item->get_path(), QSize(current_col_width_, req_height));
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
