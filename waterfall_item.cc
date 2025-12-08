#include <QPen>
#include <QBrush>
#include <QPainter>
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

waterfall_item::waterfall_item(QGraphicsItem* parent) : QGraphicsPixmapItem(parent)
{
    setFlag(QGraphicsItem::ItemIsSelectable);
    setShapeMode(QGraphicsPixmapItem::BoundingRectShape);
    setAcceptHoverEvents(true);
    setPixmap(get_placeholder());
    setTransformationMode(Qt::SmoothTransformation);
}

void waterfall_item::bind_model(const layout_model& model, int display_width, quint64 request_id)
{
    index_ = model.index;
    path_ = model.path;
    original_size_ = model.original_size;
    target_width_ = display_width;
    current_request_id_ = request_id;

    is_hovered_ = false;
    setZValue(0);
    setGraphicsEffect(nullptr);

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
    is_hovered_ = false;

    setPixmap(get_placeholder());
    setGraphicsEffect(nullptr);
    setZValue(0);
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

    base_scale_ = static_cast<qreal>(target_width_) / pixmap().width();

    setTransformOriginPoint(pixmap().rect().center());

    if (is_hovered_)
    {
        setScale(base_scale_ * 1.08);
    }
    else
    {
        setScale(base_scale_);
    }
}

void waterfall_item::hoverEnterEvent(QGraphicsSceneHoverEvent* event)
{
    setZValue(1.0);

    setScale(base_scale_ * 1.08);

    is_hovered_ = true;

    update();

    QGraphicsPixmapItem::hoverEnterEvent(event);
}

void waterfall_item::hoverLeaveEvent(QGraphicsSceneHoverEvent* event)
{
    setZValue(0.0);

    setScale(base_scale_);

    is_hovered_ = false;

    update();

    QGraphicsPixmapItem::hoverLeaveEvent(event);
}

void waterfall_item::paint(QPainter* painter, const QStyleOptionGraphicsItem* option, QWidget* widget)
{
    QGraphicsPixmapItem::paint(painter, option, widget);
    if (!is_hovered_)
    {
        return;
    }
    painter->save();
    QPen pen(QColor(255, 255, 255, 200), 6);
    pen.setJoinStyle(Qt::MiterJoin);
    painter->setPen(pen);
    painter->drawRect(pixmap().rect());
    painter->restore();
}
