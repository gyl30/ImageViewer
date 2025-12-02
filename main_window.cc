#include <QTimer>
#include <QDebug>
#include <QToolBar>
#include <QFileDialog>
#include <QDirIterator>
#include <QImageReader>

#include "main_window.h"
#include "image_loader.h"
#include "common_types.h"
#include "waterfall_view.h"
#include "waterfall_scene.h"
#include "image_viewer_window.h"

main_window::main_window(QWidget* parent) : QMainWindow(parent), view_(nullptr), scene_(nullptr), worker_thread_(nullptr), image_loader_(nullptr)
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
    auto* toolbar = addToolBar("Main");
    auto* act_add = toolbar->addAction("Add Folder");
    connect(act_add, &QAction::triggered, this, &main_window::on_add_folder);

    scene_ = new waterfall_scene(this);
    view_ = new waterfall_view(this);
    view_->setScene(scene_);

    setCentralWidget(view_);
}

void main_window::setup_worker()
{
    worker_thread_ = new QThread(this);
    image_loader_ = new image_loader();

    image_loader_->moveToThread(worker_thread_);

    connect(worker_thread_, &QThread::finished, image_loader_, &QObject::deleteLater);

    worker_thread_->start();
}

void main_window::setup_connections()
{
    connect(view_, &waterfall_view::view_resized, this, [this](int width) { scene_->layout_items(width); });
    connect(scene_, &waterfall_scene::request_load_image, image_loader_, &image_loader::request_thumbnail);
    connect(image_loader_, &image_loader::thumbnail_loaded, scene_, &waterfall_scene::on_image_loaded);
    connect(scene_, &waterfall_scene::image_double_clicked, this, &main_window::on_image_double_clicked);
}

void main_window::on_add_folder()
{
    QString dir_path = QFileDialog::getExistingDirectory(this, "Select Folder");
    if (dir_path.isEmpty())
    {
        return;
    }

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

            scene_->add_image(meta);
        }
    }

    scene_->layout_items(view_->viewport()->width());
}

void main_window::on_image_double_clicked(const QString& path)
{
    qDebug() << "Opening full image: " << path;
    auto* viewer = new image_viewer_window(nullptr);
    viewer->set_image_path(path);
    viewer->show();
}
