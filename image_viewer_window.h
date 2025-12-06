#ifndef IMAGE_VIEWER_WINDOW_H
#define IMAGE_VIEWER_WINDOW_H

#include <QMainWindow>
#include <QFutureWatcher>
#include <vector>
#include <QString>

class QGraphicsView;
class QGraphicsScene;
class QGraphicsPixmapItem;
class QPushButton;

class image_viewer_window : public QMainWindow
{
    Q_OBJECT

   public:
    explicit image_viewer_window(QWidget* parent = nullptr);
    ~image_viewer_window() override;

    void set_image_path(const QString& path);
    void set_image_list(const std::vector<QString>& paths);

   protected:
    void showEvent(QShowEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;
    bool eventFilter(QObject* watched, QEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;
    void closeEvent(QCloseEvent* event) override;
   private slots:
    void zoom_in();
    void zoom_out();
    void load_prev_image();
    void load_next_image();

   private:
    void setup_ui();
    void load_image(const QString& path);
    void navigate_image(int delta);
    void update_index_from_path();

   private:
    QString current_path_;
    std::vector<QString> image_list_;
    ptrdiff_t current_index_;

    QGraphicsView* view_;
    QGraphicsScene* scene_;
    QGraphicsPixmapItem* image_item_;

    QPushButton* btn_prev_;
    QPushButton* btn_next_;
    QFutureWatcher<QImage>* image_watcher_ = nullptr;
};

#endif
