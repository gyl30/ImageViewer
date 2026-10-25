#include <QPen>
#include <QBrush>
#include <QPainter>
#include <QApplication>
#include <QPainterPath>
#include <QPalette>
#include <algorithm>
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
    setCursor(Qt::PointingHandCursor);
}

void waterfall_item::bind_model(const layout_model& model, int index, const QString& path, int display_width, quint64 request_id)
{
    prepareGeometryChange();
    target_height_ = qRound(model.layout_rect.height());
    if (current_request_id_ != request_id || path_ != path)
    {
        setPixmap(get_placeholder());
        state_ = request_id == 0 ? load_state::empty : load_state::loading;
    }

    index_ = index;
    path_ = path;
    original_size_ = model.original_size;
    target_width_ = display_width;
    current_request_id_ = request_id;

    is_hovered_ = false;
    setZValue(0);

    setPos(model.layout_rect.topLeft());
    setToolTip(path);
    update_scale();
    setVisible(true);
}

void waterfall_item::start_request(quint64 id, qreal dpr)
{
    current_request_id_ = id;
    requested_dpr_ = dpr;
    state_ = state_ == load_state::ready ? load_state::refreshing : load_state::loading;
}

void waterfall_item::reset()
{
    index_ = -1;
    path_.clear();
    original_size_ = QSize();
    current_request_id_ = 0;
    requested_dpr_ = 0;
    is_hovered_ = false;

    setPixmap(get_placeholder());
    state_ = load_state::empty;
    setZValue(0);
    setVisible(false);
}

void waterfall_item::set_pixmap_safe(const QPixmap& pixmap)
{
    state_ = pixmap.isNull() && state_ != load_state::refreshing ? load_state::failed : load_state::ready;
    if (pixmap.isNull())
    {
        update();
        return;
    }
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

    const qreal base_scale = static_cast<qreal>(target_width_) / pixmap().width();
    setTransform(QTransform::fromScale(base_scale, base_scale));
    setTransformOriginPoint(QRectF(pixmap().rect()).center());

    setScale(1.0);
}

void waterfall_item::hoverEnterEvent(QGraphicsSceneHoverEvent* event)
{
    setZValue(1.0);

    is_hovered_ = true;

    update();

    QGraphicsPixmapItem::hoverEnterEvent(event);
}

void waterfall_item::hoverLeaveEvent(QGraphicsSceneHoverEvent* event)
{
    setZValue(0.0);

    is_hovered_ = false;

    update();

    QGraphicsPixmapItem::hoverLeaveEvent(event);
}

void waterfall_item::paint(QPainter* painter, const QStyleOptionGraphicsItem* option, QWidget* widget)
{
    Q_UNUSED(option)
    Q_UNUSED(widget)
    const QRectF bounds = boundingRect();
    const qreal scale = transform().m11();
    const qreal radius = std::min({6.0 / scale, bounds.width() / 2, bounds.height() / 2});
    const QPalette palette = QApplication::palette();
    QPainterPath clip;
    clip.addRoundedRect(bounds, radius, radius);
    painter->save();
    painter->setRenderHint(QPainter::Antialiasing);
    painter->setClipPath(clip);
    if (state_ == load_state::ready || state_ == load_state::refreshing)
    {
        painter->drawPixmap(bounds, pixmap(), QRectF(pixmap().rect()));
    }
    else
    {
        painter->fillRect(bounds, palette.alternateBase());
        if (bounds.width() * scale >= 80 && bounds.height() * scale >= 32)
        {
            QFont font = painter->font();
            font.setPixelSize(std::max(1, qRound(12 / scale)));
            painter->setFont(font);
            painter->setPen(palette.color(QPalette::Text));
            painter->drawText(bounds, Qt::AlignCenter, state_ == load_state::failed ? "无法读取" : "正在加载…");
        }
    }
    painter->restore();
    if (bounds.height() * scale < 3)
    {
        return;
    }
    painter->save();
    painter->setRenderHint(QPainter::Antialiasing);
    const bool selected = isSelected();
    const qreal inset = 1.0 / scale;
    QPen pen(selected || is_hovered_ ? palette.color(QPalette::Highlight) : palette.color(QPalette::Mid),
             (selected ? 2.0 : 1.0) / scale);
    painter->setPen(pen);
    painter->setBrush(Qt::NoBrush);
    painter->drawRoundedRect(bounds.adjusted(inset, inset, -inset, -inset), radius, radius);
    painter->restore();
}

QRectF waterfall_item::boundingRect() const
{
    if (target_width_ <= 0 || target_height_ <= 0)
    {
        return QGraphicsPixmapItem::boundingRect();
    }
    const qreal width = pixmap().width();
    return QRectF(0, 0, width, target_height_ * width / target_width_);
}
