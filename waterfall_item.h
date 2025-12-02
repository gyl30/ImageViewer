#ifndef WATERFALL_ITEM_H
#define WATERFALL_ITEM_H

#include <QGraphicsPixmapItem>
#include "common_types.h"

class waterfall_item : public QGraphicsPixmapItem
{
   public:
    explicit waterfall_item(const image_meta& meta, QGraphicsItem* parent = nullptr);

    QString get_path() const;

    bool is_loaded() const;
    void set_loaded(bool loaded);

    bool is_loading() const;
    void set_loading(bool loading);

    void set_display_width(int width);

    void set_pixmap_safe(const QPixmap& pixmap);

   private:
    void update_scale();

   private:
    QString path_;
    bool loaded_;
    bool loading_;
    int target_width_;
};

#endif    // WATERFALL_ITEM_H
