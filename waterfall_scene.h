#ifndef WATERFALL_SCENE_H
#define WATERFALL_SCENE_H

#include <vector>
#include <QHash>
#include <QStack>
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
    void add_images(const QList<image_meta>& batch);
    void layout_models(int view_width);
    void update_viewport(const QRectF& visible_rect);
    [[nodiscard]] std::vector<QString> get_all_paths() const;

   signals:
    void request_cancel_all();
    void request_load_batch(const QList<load_task>& tasks);
    void request_cancel_batch(const QList<QString>& paths);
    void image_double_clicked(QString path);
    void request_open_folder();

   public slots:
    void on_image_loaded(quint64 id, const QString& path, const QImage& image, int session_id);
    void on_tasks_dropped(const QList<QString>& paths);

   protected:
    void mouseDoubleClickEvent(QGraphicsSceneMouseEvent* event) override;
    void contextMenuEvent(QGraphicsSceneContextMenuEvent* event) override;

   private:
    waterfall_item* obtain_item();
    void recycle_item(waterfall_item* item);

   private:
    std::vector<layout_model> all_models_;
    std::vector<int> item_y_index_;
    QStack<waterfall_item*> pool_;
    QHash<int, waterfall_item*> active_items_;
    int current_col_width_ = kMinColWidth;
    std::vector<int> col_heights_;
    size_t last_layout_index_ = 0;
    int current_session_id_ = 0;
    quint64 request_counter_ = 0;
};

#endif
