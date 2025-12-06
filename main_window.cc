#include <QTimer>
#include <QDebug>
#include <QAction>
#include <QKeySequence>
#include <QFileDialog>
#include <QDirIterator>
#include <QImageReader>
#include <QStatusBar>
#include <QCoreApplication>
#include <QResizeEvent>
#include <QFileInfo>

#include "main_window.h"
#include "image_loader.h"
#include "common_types.h"
#include "waterfall_view.h"
#include "waterfall_scene.h"
#include "waterfall_item.h"
#include "image_viewer_window.h"
#include "file_scanner.h"

main_window::main_window(QWidget* parent) : QMainWindow(parent)
{
    setup_ui();
    setup_worker();
    setup_scanner();
    setup_connections();
    resize(1024, 768);
}

main_window::~main_window()
{
    if (worker_thread_ != nullptr)
    {
        worker_thread_->quit();
        worker_thread_->wait();
    }

    if (file_scanner_ != nullptr)
    {
        file_scanner_->stop_scan();
    }
    if (scan_thread_ != nullptr)
    {
        scan_thread_->quit();
        scan_thread_->wait();
    }
}

void main_window::setup_ui()
{
    auto* act_open = new QAction("Open", this);
    act_open->setShortcut(QKeySequence::Open);
    addAction(act_open);
    connect(act_open, &QAction::triggered, this, &main_window::on_add_folder);

    scene_ = new waterfall_scene(this);
    view_ = new waterfall_view(this);
    view_->setScene(scene_);

    setCentralWidget(view_);

    status_label_ = new QLabel("Press Ctrl+O to load folder", this);
    statusBar()->addWidget(status_label_);

    info_label_ = new QLabel(this);
    statusBar()->addPermanentWidget(info_label_);
}

void main_window::setup_worker()
{
    worker_thread_ = new QThread(this);
    image_loader_ = new image_loader();

    image_loader_->moveToThread(worker_thread_);

    connect(worker_thread_, &QThread::finished, image_loader_, &QObject::deleteLater);

    worker_thread_->start();
}

void main_window::setup_scanner()
{
    scan_thread_ = new QThread(this);
    file_scanner_ = new file_scanner();

    file_scanner_->moveToThread(scan_thread_);

    connect(scan_thread_, &QThread::finished, file_scanner_, &QObject::deleteLater);
    connect(this, &main_window::request_start_scan, file_scanner_, &file_scanner::start_scan);
    connect(file_scanner_, &file_scanner::images_scanned_batch, this, &main_window::on_scan_batch_received);
    connect(file_scanner_, &file_scanner::scan_finished, this, &main_window::on_scan_all_finished);

    scan_thread_->start();
}

void main_window::setup_connections()
{
    connect(scene_, &waterfall_scene::request_cancel_all, image_loader_, &image_loader::clear_all, Qt::QueuedConnection);
    connect(scene_, &waterfall_scene::request_load_batch, image_loader_, &image_loader::request_thumbnails, Qt::QueuedConnection);
    connect(scene_, &waterfall_scene::request_cancel_batch, image_loader_, &image_loader::cancel_thumbnails, Qt::QueuedConnection);

    connect(image_loader_, &image_loader::thumbnail_loaded, scene_, &waterfall_scene::on_image_loaded);
    connect(image_loader_, &image_loader::tasks_dropped, scene_, &waterfall_scene::on_tasks_dropped, Qt::QueuedConnection);

    connect(view_, &waterfall_view::view_resized, this, [this](int width) { scene_->layout_items(width); });

    connect(scene_, &waterfall_scene::image_double_clicked, this, &main_window::on_image_double_clicked);
    connect(scene_, &waterfall_scene::request_open_folder, this, &main_window::on_add_folder);
    connect(scene_, &QGraphicsScene::selectionChanged, this, &main_window::on_selection_changed);
    connect(image_loader_, &image_loader::thumbnail_loaded, this, &main_window::on_image_loaded_stat);
}

void main_window::on_add_folder()
{
    QString dir_path = QFileDialog::getExistingDirectory(this, "Select Folder", QDir::homePath());

    if (dir_path.isEmpty())
    {
        return;
    }

    if (file_scanner_ != nullptr)
    {
        file_scanner_->stop_scan();
    }

    scene_->clear_items();
    total_count_ = 0;
    loaded_count_ = 0;
    scan_duration_ = 0;
    info_label_->clear();

    status_label_->setText(QString("Scanning: %1").arg(dir_path));

    emit request_start_scan(dir_path);
}

void main_window::on_scan_batch_received(const QList<image_meta>& batch)
{
    for (const auto& meta : batch)
    {
        scene_->add_image(meta);
    }

    total_count_ += static_cast<int>(batch.size());

    scene_->layout_items(view_->viewport()->width());

    QMetaObject::invokeMethod(view_, [this]() { view_->check_visible_area(); }, Qt::QueuedConnection);

    update_status_bar();
}

void main_window::on_scan_all_finished(int total, qint64 duration)
{
    scan_duration_ = duration;
    total_count_ = total;

    scene_->layout_items(view_->viewport()->width());
    QMetaObject::invokeMethod(view_, [this]() { view_->check_visible_area(); }, Qt::QueuedConnection);

    update_status_bar();
}

void main_window::on_image_loaded_stat()
{
    loaded_count_++;
    update_status_bar();
}

void main_window::update_status_bar()
{
    QString status = QString("Scan+Layout: %1 ms | Loaded: %2 / %3").arg(scan_duration_).arg(loaded_count_).arg(total_count_);

    if (loaded_count_ == total_count_ && total_count_ > 0)
    {
        status += " [All Done]";
    }
    else if (total_count_ == 0 && scan_duration_ > 0)
    {
        status = "No images found in selected folder.";
    }

    status_label_->setText(status);
}

void main_window::on_selection_changed()
{
    QList<QGraphicsItem*> items = scene_->selectedItems();

    if (items.isEmpty())
    {
        info_label_->clear();
        return;
    }

    auto* item = dynamic_cast<waterfall_item*>(items.first());
    if (item == nullptr)
    {
        return;
    }

    QFileInfo file_info(item->get_path());
    QString file_name = file_info.fileName();
    qint64 file_bytes = file_info.size();
    QSize img_size = item->get_original_size();

    QString size_str;
    if (file_bytes < 1024)
    {
        size_str = QString("%1 B").arg(file_bytes);
    }
    else if (file_bytes < static_cast<qint64>(1024 * 1024))
    {
        size_str = QString("%1 KB").arg(static_cast<double>(file_bytes) / 1024.0, 0, 'f', 1);
    }
    else
    {
        size_str = QString("%1 MB").arg(static_cast<double>(file_bytes) / (1024.0 * 1024.0), 0, 'f', 2);
    }

    info_label_->setText(QString("%1 | %2x%3 | %4").arg(file_name).arg(img_size.width()).arg(img_size.height()).arg(size_str));
}

void main_window::on_image_double_clicked(const QString& path)
{
    if (viewer_window_ == nullptr)
    {
        viewer_window_ = new image_viewer_window(this);
        viewer_window_->setWindowFlags(Qt::Window);
    }

    viewer_window_->set_image_list(scene_->get_all_paths());

    viewer_window_->set_image_path(path);

    if (viewer_window_->isMinimized())
    {
        viewer_window_->showNormal();
    }
    viewer_window_->show();
    viewer_window_->raise();
    viewer_window_->activateWindow();
}
