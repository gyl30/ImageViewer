#ifndef WATERFALL_SCENE_H
#define WATERFALL_SCENE_H

#include <vector>
#include <QHash>
#include <QObject>
#include <QGraphicsScene>
#include <QGraphicsSceneContextMenuEvent>
#include "common_types.h"

class waterfall_item;

class waterfall_scene : public QGraphicsScene
{
    Q_OBJECT

   public:
    explicit waterfall_scene(QObject* parent = nullptr);

    void clear_items();

    void add_image(const image_meta& meta);

    void layout_items(int view_width);

    void load_visible_items(const QRectF& visible_rect);

   signals:
    void request_load_image(QString path, QSize size);
    void request_cancel_image(QString path);
    void image_double_clicked(QString path);
    void request_open_folder();

   public slots:
    void on_image_loaded(const QString& path, const QImage& image);

   protected:
    void mouseDoubleClickEvent(QGraphicsSceneMouseEvent* event) override;

    void contextMenuEvent(QGraphicsSceneContextMenuEvent* event) override;

   private:
    std::vector<waterfall_item*> items_;
    QHash<QString, waterfall_item*> item_map_;

    int current_col_width_;
};

#endif
