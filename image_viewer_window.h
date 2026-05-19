#ifndef IMAGE_VIEWER_WINDOW_H
#define IMAGE_VIEWER_WINDOW_H

#include <QMainWindow>
#include <QFutureWatcher>
#include <QCloseEvent>
#include <vector>
#include <QString>
#include <utility>

class QGraphicsView;
class QGraphicsScene;
class QGraphicsPixmapItem;
class QPushButton;
class QAction;
class QActionGroup;

class image_viewer_window : public QMainWindow
{
    Q_OBJECT

   public:
    enum class view_mode
    {
        fit_window,
        actual_size,
        fit_width
    };

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
    void set_fit_window_mode();
    void set_actual_size_mode();
    void set_fit_width_mode();
    void toggle_full_screen();
    void zoom_in();
    void zoom_out();
    void load_prev_image();
    void load_next_image();

   private:
    void setup_ui();
    void load_image(const QString& path);
    void navigate_image(int delta);
    void resize_window_to_image(const QSize& image_size);
    void apply_auto_view();
    void load_settings();
    void save_settings() const;
    void update_view_mode_actions();
    void update_index_from_path();
    void update_navigation_buttons();

   private:
    QString current_path_;
    std::vector<QString> image_list_;
    ptrdiff_t current_index_;
    bool has_manual_zoom_ = false;
    view_mode current_view_mode_ = view_mode::fit_window;

    QGraphicsView* view_;
    QGraphicsScene* scene_;
    QGraphicsPixmapItem* image_item_;

    QPushButton* btn_prev_;
    QPushButton* btn_next_;
    QActionGroup* view_mode_group_ = nullptr;
    QAction* fit_window_action_ = nullptr;
    QAction* actual_size_action_ = nullptr;
    QAction* fit_width_action_ = nullptr;
    QAction* full_screen_action_ = nullptr;

    QFutureWatcher<std::pair<QImage, QString>>* image_watcher_ = nullptr;
};

#endif
