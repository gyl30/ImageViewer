#ifndef MAIN_WINDOW_H
#define MAIN_WINDOW_H

#include <QMainWindow>
#include <QThread>

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

    void on_add_folder();
    void reload_layout();

    static void on_image_double_clicked(const QString& path);

   private:
    waterfall_view* view_;
    waterfall_scene* scene_;

    QThread* worker_thread_;
    image_loader* image_loader_;
};

#endif    // MAIN_WINDOW_H
