#ifndef IMAGE_VIEWER_WINDOW_H
#define IMAGE_VIEWER_WINDOW_H

#include <QMainWindow>
#include <QFutureWatcher>
#include <QCloseEvent>
#include <QString>
#include <QStringList>
#include <utility>

class QGraphicsView;
class QGraphicsScene;
class QGraphicsPixmapItem;
class QLabel;
class QAction;
class QActionGroup;
class QTimer;
class QMovie;

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
    void set_image_list(const QStringList& paths);
    void remove_image_path(const QString& path);
    [[nodiscard]] QString current_image_path() const { return current_path_; }

   signals:
    void current_image_changed(QString path);
    void request_move_to_trash(QString path);

   protected:
    void resizeEvent(QResizeEvent* event) override;
    bool eventFilter(QObject* watched, QEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;
    void closeEvent(QCloseEvent* event) override;
   private slots:
    void set_fit_window_mode();
    void set_actual_size_mode();
    void set_fit_width_mode();
    void toggle_full_screen();
    void rotate_left();
    void rotate_right();
    void flip_horizontal();
    void flip_vertical();
    void toggle_slideshow();
    void toggle_slideshow_loop();
    void configure_slideshow_interval();
    void advance_slideshow();
    void zoom_in();
    void zoom_out();
    void load_prev_image();
    void load_next_image();

   private:
    void setup_ui();
    void load_image(const QString& path);
    void navigate_image(int delta);
    void apply_auto_view();
    void load_settings();
    void save_settings() const;
    void display_image(QImage image, const QString& path);
    void apply_image_transform();
    void clear_movie();
    void start_movie(const QString& path);
    void update_image_status(const QString& path, const QSize& image_size);
    void update_zoom_status();
    void update_view_mode_actions();
    void update_index_from_path();
    void update_navigation_buttons();

   private:
    QString current_path_;
    QString loading_path_;
    QStringList image_list_;
    ptrdiff_t current_index_;
    bool has_manual_zoom_ = false;
    view_mode current_view_mode_ = view_mode::fit_window;
    int rotation_degrees_ = 0;
    bool flip_horizontal_ = false;
    bool flip_vertical_ = false;

    QGraphicsView* view_;
    QGraphicsScene* scene_;
    QGraphicsPixmapItem* image_item_;

    QAction* prev_action_;
    QAction* next_action_;
    QLabel* position_label_ = nullptr;
    QActionGroup* view_mode_group_ = nullptr;
    QAction* fit_window_action_ = nullptr;
    QAction* actual_size_action_ = nullptr;
    QAction* fit_width_action_ = nullptr;
    QAction* full_screen_action_ = nullptr;
    QAction* rotate_left_action_ = nullptr;
    QAction* rotate_right_action_ = nullptr;
    QAction* flip_horizontal_action_ = nullptr;
    QAction* flip_vertical_action_ = nullptr;
    QAction* slideshow_action_ = nullptr;
    QAction* slideshow_loop_action_ = nullptr;
    QAction* slideshow_interval_action_ = nullptr;
    QLabel* image_info_label_ = nullptr;
    QLabel* zoom_label_ = nullptr;
    QTimer* slideshow_timer_ = nullptr;
    QMovie* movie_ = nullptr;
    bool slideshow_loop_enabled_ = false;
    int slideshow_interval_ms_ = 3000;

    QFutureWatcher<std::pair<QImage, QString>>* image_watcher_ = nullptr;
};

#endif
