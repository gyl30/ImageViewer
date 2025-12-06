#ifndef MAIN_WINDOW_H
#define MAIN_WINDOW_H

#include <QMainWindow>
#include <QThread>
#include <QLabel>
#include <QElapsedTimer>
#include <QList>
#include "common_types.h"
#include "file_scanner.h"

class waterfall_view;
class waterfall_scene;
class image_loader;
class image_viewer_window;

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
    void setup_scanner();
    void update_status_bar();

    void on_add_folder();
    void on_image_loaded_stat();
    void on_selection_changed();
    void on_image_double_clicked(const QString& path);

    void on_scan_batch_received(const QList<image_meta>& batch);
    void on_scan_all_finished(int total, qint64 duration);

   signals:
    void request_start_scan(const QString& path);

   private:
    waterfall_view* view_ = nullptr;
    waterfall_scene* scene_ = nullptr;

    QThread* worker_thread_ = nullptr;
    image_loader* image_loader_ = nullptr;

    QThread* scan_thread_ = nullptr;
    file_scanner* file_scanner_ = nullptr;

    QLabel* status_label_ = nullptr;
    QLabel* info_label_ = nullptr;
    qint64 scan_duration_;
    int total_count_ = 0;
    int loaded_count_ = 0;

    image_viewer_window* viewer_window_ = nullptr;
};

#endif
