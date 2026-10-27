#ifndef MAIN_WINDOW_H
#define MAIN_WINDOW_H

#include <QMainWindow>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QThread>
#include <QLabel>
#include <QElapsedTimer>
#include <QList>
#include <QStringList>
#include <QCloseEvent>
#include "common_types.h"
#include "file_scanner.h"

class QImage;
class QAction;
class QActionGroup;
class QProgressBar;
class waterfall_view;
class waterfall_scene;
class image_loader;
class image_viewer_window;

class main_window : public QMainWindow
{
    Q_OBJECT
    friend class performance_probe;

   public:
    explicit main_window(QWidget* parent = nullptr);
    ~main_window() override;

   private:
    enum class scan_sort_mode
    {
        file_name = 0,
        modified_time = 1,
        file_size = 2
    };

    void setup_ui();
    void setup_connections();
    void setup_worker();
    void setup_scanner();
    void update_status_bar();
    void load_settings();
    void save_settings() const;
    void set_dark_theme(bool enabled);
    void prune_recent_paths();
    void open_path(const QString& path, bool add_to_recent);
    void add_recent_path(const QString& path);
    void show_recent_menu(const QPoint& global_pos);
    void show_image_viewer(const QString& path, const QStringList& image_list);

    void on_add_folder();
    void on_selection_changed();
    void on_image_double_clicked(const QString& path);
    void on_open_recent_path(const QString& path);
    void on_reveal_path(const QString& path);
    void on_move_path_to_trash(const QString& path);

    void on_scan_batch_received(const QList<image_meta>& batch, int session_id);
    void on_scan_all_finished(int total, qint64 duration, int session_id);

   protected:
    void closeEvent(QCloseEvent* event) override;
    void dragEnterEvent(QDragEnterEvent* event) override;
    void dropEvent(QDropEvent* event) override;

   signals:
    void request_start_scan(const QString& path, int session_id, int sort_mode, bool descending);

   private:
    waterfall_view* view_ = nullptr;
    waterfall_scene* scene_ = nullptr;
    QThread* worker_thread_ = nullptr;
    image_loader* image_loader_ = nullptr;
    QThread* scan_thread_ = nullptr;
    file_scanner* file_scanner_ = nullptr;
    QLabel* status_label_ = nullptr;
    QLabel* info_label_ = nullptr;
    QProgressBar* scan_progress_ = nullptr;
    QAction* dark_theme_action_ = nullptr;
    bool is_scanning_ = false;
    qint64 scan_duration_ = 0;
    int total_count_ = 0;
    QStringList recent_folder_paths_;
    QStringList recent_image_paths_;
    QString last_open_dir_;
    QString current_root_path_;
    int current_scan_session_id_ = 0;
    scan_sort_mode current_sort_mode_ = scan_sort_mode::file_name;
    bool sort_descending_ = false;
    QActionGroup* sort_group_ = nullptr;
    QAction* sort_name_action_ = nullptr;
    QAction* sort_time_action_ = nullptr;
    QAction* sort_size_action_ = nullptr;
    QAction* sort_desc_action_ = nullptr;
    image_viewer_window* viewer_window_ = nullptr;
};

#endif
