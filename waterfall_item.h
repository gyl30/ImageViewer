#ifndef WATERFALL_ITEM_H
#define WATERFALL_ITEM_H

#include <QGraphicsPixmapItem>
#include <QSize>
#include "common_types.h"

class waterfall_item : public QGraphicsPixmapItem
{
   public:
    explicit waterfall_item(const image_meta& meta, QGraphicsItem* parent = nullptr);

    [[nodiscard]] QString get_path() const;
    [[nodiscard]] QSize get_original_size() const;

    [[nodiscard]] bool is_loaded() const;
    void set_loaded(bool loaded);

    [[nodiscard]] bool is_loading() const;
    void set_loading(bool loading);

    void set_display_width(int width);

    void set_pixmap_safe(const QPixmap& pixmap);

    void unload();

   private:
    void update_scale();

   private:
    QString path_;
    QSize original_size_;
    bool loaded_;
    bool loading_;
    int target_width_;
};

#endif    // WATERFALL_ITEM_H
