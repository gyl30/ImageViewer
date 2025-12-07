#include <QBrush>
#include "waterfall_item.h"

static QPixmap& get_placeholder()
{
    static QPixmap p(1, 1);
    if (p.width() == 1)
    {
        p = QPixmap(200, 200);
        p.fill(Qt::lightGray);
    }
    return p;
}

waterfall_item::waterfall_item(QGraphicsItem* parent)
    : QGraphicsPixmapItem(parent)
{
    setFlag(QGraphicsItem::ItemIsSelectable);
    setShapeMode(QGraphicsPixmapItem::BoundingRectShape);
    setPixmap(get_placeholder());
}

void waterfall_item::bind_model(const layout_model& model, int display_width, quint64 request_id)
{
    index_ = model.index;
    path_ = model.path;
    original_size_ = model.original_size; 
    target_width_ = display_width;
    current_request_id_ = request_id;

    setPos(model.layout_rect.topLeft());
    setPixmap(get_placeholder());
    update_scale();
    setVisible(true);
}

void waterfall_item::reset()
{
    index_ = -1;
    path_.clear();
    original_size_ = QSize();
    current_request_id_ = 0;
    setPixmap(get_placeholder());
    setVisible(false);
}

void waterfall_item::set_pixmap_safe(const QPixmap& pixmap)
{
    if (this->pixmap().cacheKey() == pixmap.cacheKey())
    {
        return;
    }
    setPixmap(pixmap);
    update_scale();
}

void waterfall_item::update_scale()
{
    if (pixmap().isNull() || pixmap().width() == 0 || target_width_ <= 0)
    {
        return;
    }
    qreal s = static_cast<qreal>(target_width_) / pixmap().width();
    setScale(s);
}
