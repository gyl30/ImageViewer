#ifndef WATERFALL_ITEM_H
#define WATERFALL_ITEM_H

#include <QGraphicsPixmapItem>
#include "common_types.h"

class waterfall_item : public QGraphicsPixmapItem
{
public:
    explicit waterfall_item(const image_meta& meta, QGraphicsItem* parent = nullptr);
    
    QString get_path() const;

private:
    QString path_;
};

#endif // WATERFALL_ITEM_H
