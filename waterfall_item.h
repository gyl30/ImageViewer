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

    [[nodiscard]] bool wants_loading() const;
    void set_wants_loading(bool wants);

    void set_display_width(int width);
    void set_pixmap_safe(const QPixmap& pixmap);
    void unload();

    void set_request_id(quint64 id) { current_request_id_ = id; }
    [[nodiscard]] quint64 get_request_id() const { return current_request_id_; }

   private:
    void update_scale();

   private:
    QString path_;
    QSize original_size_;
    bool loaded_;
    bool loading_;
    bool wants_loading_;
    int target_width_;
    quint64 current_request_id_ = 0;
};

#endif
