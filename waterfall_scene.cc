#include <algorithm>
#include <cmath>
#include <QGraphicsSceneMouseEvent>

#include "waterfall_item.h"
#include "waterfall_scene.h"

waterfall_scene::waterfall_scene(QObject* parent) : QGraphicsScene(parent), column_count_(1) {}

void waterfall_scene::add_image(const image_meta& meta)
{
    auto* item = new waterfall_item(meta);
    addItem(item);
    items_.push_back(item);

    double ratio = 1.0;
    if (meta.original_size.isValid() && meta.original_size.width() > 0)
    {
        ratio = static_cast<double>(meta.original_size.height()) / meta.original_size.width();
    }

    int thumb_height = static_cast<int>(kThumbWidth * ratio);

    QPixmap placeholder(kThumbWidth, thumb_height);
    placeholder.fill(QColor(230, 230, 230));
    item->setPixmap(placeholder);
}

void waterfall_scene::layout_items(int view_width)
{
    if (items_.empty() || view_width <= 0)
    {
        return;
    }

    int full_col_width = kThumbWidth + kColumnMargin;
    int available_width = view_width - (2 * kItemMargin);

    int new_col_count = std::max(1, available_width / full_col_width);

    std::vector<int> col_heights(new_col_count, kItemMargin);

    for (auto* item : items_)
    {
        auto min_itr = std::min_element(col_heights.begin(), col_heights.end());
        int min_col_idx = static_cast<int>(std::distance(col_heights.begin(), min_itr));
        int x = kItemMargin + (min_col_idx * full_col_width);
        int y = *min_itr;

        item->setPos(x, y);

        int item_height = item->pixmap().height();

        col_heights[min_col_idx] += (item_height + kItemMargin);

        emit request_load_image(item->get_path(), QSize(kThumbWidth, item_height));
    }

    int max_height = *std::max_element(col_heights.begin(), col_heights.end());
    setSceneRect(0, 0, view_width, max_height + 50);
}

void waterfall_scene::on_image_loaded(const QString& path, const QPixmap& pixmap)
{
    for (auto* item : items_)
    {
        if (item->get_path() == path)
        {
            item->setPixmap(pixmap);
            break;
        }
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
