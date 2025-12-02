#ifndef WATERFALL_SCENE_H
#define WATERFALL_SCENE_H

#include <QGraphicsScene>
#include <vector>
#include <QObject>
#include "common_types.h"

class waterfall_item;

class waterfall_scene : public QGraphicsScene
{
    Q_OBJECT

   public:
    explicit waterfall_scene(QObject* parent = nullptr);

    void add_image(const image_meta& meta);

    void layout_items(int view_width);

   signals:
    void request_load_image(QString path, QSize size);

    void image_double_clicked(QString path);

   public slots:
    void on_image_loaded(const QString& path, const QPixmap& pixmap);

   protected:
    void mouseDoubleClickEvent(QGraphicsSceneMouseEvent* event) override;

   private:
    std::vector<waterfall_item*> items_;
    int column_count_;
};

#endif    // WATERFALL_SCENE_H
