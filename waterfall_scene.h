#ifndef WATERFALL_SCENE_H
#define WATERFALL_SCENE_H

#include <vector>
#include <QHash>
#include <QSet>
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
    [[nodiscard]] std::vector<QString> get_all_paths() const;

   signals:
    void request_cancel_all();
    void request_load_image(quint64 id, QString path, QSize size, int session_id);
    void request_cancel_image(QString path);
    void image_double_clicked(QString path);
    void request_open_folder();

   public slots:
    void on_image_loaded(quint64 id, const QString& path, const QImage& image, int session_id);

   protected:
    void mouseDoubleClickEvent(QGraphicsSceneMouseEvent* event) override;
    void contextMenuEvent(QGraphicsSceneContextMenuEvent* event) override;

   private:
    std::vector<waterfall_item*> items_;
    QHash<QString, waterfall_item*> item_map_;
    QSet<waterfall_item*> loaded_items_;

    int current_col_width_;
    std::vector<int> col_heights_;
    size_t last_layout_item_index_;
    int current_session_id_ = 0;
};

#endif
