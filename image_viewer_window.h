#ifndef IMAGE_VIEWER_WINDOW_H
#define IMAGE_VIEWER_WINDOW_H

#include <QFuture>
#include <QMainWindow>

class QGraphicsView;
class QGraphicsScene;
class QGraphicsPixmapItem;

class image_viewer_window : public QMainWindow
{
    Q_OBJECT

   public:
    explicit image_viewer_window(QWidget* parent = nullptr);
    ~image_viewer_window() override;

    void set_image_path(const QString& path);

   protected:
    void showEvent(QShowEvent* event) override;

    void wheelEvent(QWheelEvent* event) override;

   private:
    void setup_ui();
    void load_image(const QString& path);

   private:
    QString current_path_;
    QGraphicsView* view_;
    QGraphicsScene* scene_;
    QGraphicsPixmapItem* image_item_;
    QFuture<void> load_future_;
};

#endif    // IMAGE_VIEWER_WINDOW_H
