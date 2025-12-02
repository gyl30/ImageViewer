#ifndef MAIN_WINDOW_H
#define MAIN_WINDOW_H

#include <QMainWindow>
#include <QThread>
#include <QLabel>
#include <QElapsedTimer>
#include <QFutureWatcher>
#include <vector>
#include "common_types.h"

class waterfall_view;
class waterfall_scene;
class image_loader;

class main_window : public QMainWindow
{
    Q_OBJECT

   public:
    explicit main_window(QWidget* parent = nullptr);
    ~main_window() override;

   private:
    void setup_ui();
    void setup_connections();
    void setup_worker();
    void update_status_bar();

    void on_add_folder();
    void on_scan_finished();
    void on_image_loaded_stat();
    void on_selection_changed();
    void on_image_double_clicked(const QString& path);

   private:
    waterfall_view* view_;
    waterfall_scene* scene_;

    QThread* worker_thread_;
    image_loader* image_loader_;

    QLabel* status_label_;
    QLabel* info_label_;
    QElapsedTimer scan_timer_;
    qint64 scan_duration_;
    int total_count_;
    int loaded_count_;

    QFutureWatcher<std::vector<image_meta>>* scan_watcher_;
};

#endif
