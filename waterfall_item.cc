#include <QBrush>
#include <QPen>
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
waterfall_item::waterfall_item(const image_meta& meta, QGraphicsItem* parent)
    : QGraphicsPixmapItem(parent),
      path_(meta.path),
      original_size_(meta.original_size),
      loaded_(false),
      loading_(false),
      wants_loading_(false),
      target_width_(kMinColWidth)
{
    setFlag(QGraphicsItem::ItemIsSelectable);
    setShapeMode(QGraphicsPixmapItem::BoundingRectShape);
    setPixmap(get_placeholder());
}

QString waterfall_item::get_path() const { return path_; }

QSize waterfall_item::get_original_size() const { return original_size_; }

bool waterfall_item::is_loaded() const { return loaded_; }
void waterfall_item::set_loaded(bool loaded) { loaded_ = loaded; }

bool waterfall_item::is_loading() const { return loading_; }
void waterfall_item::set_loading(bool loading) { loading_ = loading; }

bool waterfall_item::wants_loading() const { return wants_loading_; }
void waterfall_item::set_wants_loading(bool wants) { wants_loading_ = wants; }

void waterfall_item::set_display_width(int width)
{
    if (width <= 0 || width == target_width_)
    {
        return;
    }
    target_width_ = width;
    update_scale();
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

void waterfall_item::unload()
{
    if (!loaded_)
    {
        return;
    }

    int h = 200;
    if (original_size_.isValid() && original_size_.width() > 0)
    {
        double ratio = static_cast<double>(original_size_.height()) / original_size_.width();
        h = static_cast<int>(target_width_ * ratio);
    }

    setPixmap(get_placeholder());

    loaded_ = false;
    loading_ = false;
    wants_loading_ = false;
    update_scale();
}

void waterfall_item::update_scale()
{
    if (pixmap().isNull() || pixmap().width() == 0)
    {
        return;
    }

    qreal s = static_cast<qreal>(target_width_) / pixmap().width();
    setScale(s);
}
