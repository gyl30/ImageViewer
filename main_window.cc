#include <QTimer>
#include <QDebug>
#include <QAction>
#include <QKeySequence>
#include <QFileDialog>
#include <QDirIterator>
#include <QImageReader>
#include <QStatusBar>
#include <QtConcurrent>
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

main_window::main_window(QWidget* parent)
    : QMainWindow(parent),
      view_(nullptr),
      scene_(nullptr),
      worker_thread_(nullptr),
      image_loader_(nullptr),
      status_label_(nullptr),
      info_label_(nullptr),
      scan_duration_(0),
      total_count_(0),
      loaded_count_(0),
      scan_watcher_(new QFutureWatcher<std::vector<image_meta>>(this)),
      viewer_window_(nullptr)
{
    setup_ui();
    setup_worker();
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

    connect(worker_thread_, &QThread::started, image_loader_, &image_loader::start_processing);
    connect(worker_thread_, &QThread::finished, image_loader_, &QObject::deleteLater);

    worker_thread_->start();
}

void main_window::setup_connections()
{
    connect(view_, &waterfall_view::view_resized, this, [this](int width) { scene_->layout_items(width); });

    connect(scene_, &waterfall_scene::request_load_image, image_loader_, &image_loader::request_thumbnail, Qt::DirectConnection);
    connect(scene_, &waterfall_scene::request_cancel_image, image_loader_, &image_loader::cancel_thumbnail, Qt::DirectConnection);

    connect(image_loader_, &image_loader::thumbnail_loaded, scene_, &waterfall_scene::on_image_loaded);
    connect(scene_, &waterfall_scene::image_double_clicked, this, &main_window::on_image_double_clicked);
    connect(scene_, &waterfall_scene::request_open_folder, this, &main_window::on_add_folder);
    connect(scene_, &QGraphicsScene::selectionChanged, this, &main_window::on_selection_changed);

    connect(image_loader_, &image_loader::thumbnail_loaded, this, &main_window::on_image_loaded_stat);
    connect(scan_watcher_, &QFutureWatcher<std::vector<image_meta>>::finished, this, &main_window::on_scan_finished);
}

void main_window::on_add_folder()
{
    QString dir_path = QFileDialog::getExistingDirectory(this, "Select Folder", QDir::homePath());

    if (dir_path.isEmpty())
    {
        return;
    }

    scene_->clear();
    total_count_ = 0;
    loaded_count_ = 0;
    scan_duration_ = 0;
    info_label_->clear();

    scan_timer_.start();
    status_label_->setText(QString("Scanning: %1").arg(dir_path));

    QFuture<std::vector<image_meta>> future = QtConcurrent::run(
        [dir_path]()
        {
            std::vector<image_meta> results;
            QDirIterator it(dir_path, QStringList() << "*.jpg" << "*.png" << "*.jpeg" << "*.webp", QDir::Files, QDirIterator::Subdirectories);

            while (it.hasNext())
            {
                QString file_path = it.next();

                QImageReader reader(file_path);
                QSize size = reader.size();

                if (size.isValid())
                {
                    image_meta meta;
                    meta.path = file_path;
                    meta.original_size = size;
                    results.push_back(meta);
                }
            }
            return results;
        });

    scan_watcher_->setFuture(future);
}

void main_window::on_scan_finished()
{
    std::vector<image_meta> metas = scan_watcher_->result();

    total_count_ = static_cast<int>(metas.size());

    for (const auto& meta : metas)
    {
        scene_->add_image(meta);
    }

    scene_->layout_items(view_->viewport()->width());

    scan_duration_ = scan_timer_.elapsed();
    update_status_bar();

    QMetaObject::invokeMethod(view_, [this]() { view_->check_visible_area(); }, Qt::QueuedConnection);
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
        viewer_window_ = new image_viewer_window(nullptr);

        connect(viewer_window_, &QObject::destroyed, this, [this]() { viewer_window_ = nullptr; });
    }

    if (scan_watcher_->isFinished())
    {
        std::vector<QString> paths;
        std::vector<image_meta> metas = scan_watcher_->result();
        paths.reserve(metas.size());
        for (const auto& meta : metas)
        {
            paths.push_back(meta.path);
        }
        viewer_window_->set_image_list(paths);
    }

    viewer_window_->set_image_path(path);

    if (viewer_window_->isMinimized())
    {
        viewer_window_->showNormal();
    }
    viewer_window_->show();
    viewer_window_->raise();
    viewer_window_->activateWindow();
}
