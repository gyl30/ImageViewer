#include <QBrush>
#include <QPen>
#include "waterfall_item.h"

waterfall_item::waterfall_item(const image_meta& meta, QGraphicsItem* parent) : QGraphicsPixmapItem(parent), path_(meta.path)
{
    setFlag(QGraphicsItem::ItemIsSelectable);

    QPixmap placeholder(kThumbWidth, 200);
    placeholder.fill(Qt::lightGray);
    setPixmap(placeholder);
}

QString waterfall_item::get_path() const { return path_; }
