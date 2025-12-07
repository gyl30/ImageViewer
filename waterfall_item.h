#ifndef WATERFALL_ITEM_H
#define WATERFALL_ITEM_H

#include <QGraphicsPixmapItem>
#include <QSize>
#include "common_types.h"

class waterfall_item : public QGraphicsPixmapItem
{
   public:
    explicit waterfall_item(QGraphicsItem* parent = nullptr);

    void bind_model(const layout_model& model, int display_width, quint64 request_id);
    void reset();

    [[nodiscard]] int get_index() const { return index_; }
    [[nodiscard]] QString get_path() const { return path_; }
    [[nodiscard]] QSize get_original_size() const { return original_size_; }
    [[nodiscard]] quint64 get_request_id() const { return current_request_id_; }
    void set_pixmap_safe(const QPixmap& pixmap);

   private:
    void update_scale();

   private:
    int index_ = -1;
    QString path_;
    QSize original_size_;
    int target_width_ = 0;
    quint64 current_request_id_ = 0;
};

#endif
