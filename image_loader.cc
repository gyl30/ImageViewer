#include "image_loader.h"
#include <QImageReader>
#include <QDebug>
#include <QMetaObject>

image_loader::image_loader(QObject* parent) : QObject(parent) { cache_.setMaxCost(200L * 1024 * 1024); }

image_loader::~image_loader() = default;

void image_loader::request_thumbnail(const QString& path, const QSize& target_size)
{
    if (cache_.contains(path))
    {
        emit thumbnail_loaded(path, *cache_.object(path));
        return;
    }

    if (pending_paths_.contains(path))
    {
        return;
    }

    task_queue_.append({path, target_size});
    pending_paths_.insert(path);

    if (!is_processing_)
    {
        is_processing_ = true;
        process_next_task();
    }
}

void image_loader::cancel_thumbnail(const QString& path)
{
    if (pending_paths_.contains(path))
    {
        pending_paths_.remove(path);

        auto it = std::remove_if(task_queue_.begin(), task_queue_.end(), [&path](const load_task& task) { return task.path == path; });
        task_queue_.erase(it, task_queue_.end());
    }
}

void image_loader::clear_all()
{
    task_queue_.clear();
    pending_paths_.clear();
    cache_.clear();
}

void image_loader::process_next_task()
{
    if (task_queue_.isEmpty())
    {
        is_processing_ = false;
        return;
    }

    load_task current_task = task_queue_.takeLast();
    if (!pending_paths_.contains(current_task.path))
    {
        QMetaObject::invokeMethod(this, "process_next_task", Qt::QueuedConnection);
        return;
    }
    if (cache_.contains(current_task.path))
    {
        emit thumbnail_loaded(current_task.path, *cache_.object(current_task.path));
        pending_paths_.remove(current_task.path);
        QMetaObject::invokeMethod(this, "process_next_task", Qt::QueuedConnection);
        return;
    }

    QImageReader reader(current_task.path);
    reader.setAutoTransform(true);
    if (reader.supportsOption(QImageIOHandler::ScaledSize) && !current_task.target_size.isEmpty())
    {
        reader.setScaledSize(current_task.target_size);
    }

    QImage image = reader.read();
    if (!image.isNull())
    {
        if (!current_task.target_size.isEmpty() && image.size() != current_task.target_size)
        {
            image = image.scaled(current_task.target_size, Qt::KeepAspectRatio, Qt::SmoothTransformation);
        }
        auto cost = image.sizeInBytes();
        cache_.insert(current_task.path, new QImage(image), cost);
        emit thumbnail_loaded(current_task.path, image);
    }
    pending_paths_.remove(current_task.path);
    QMetaObject::invokeMethod(this, "process_next_task", Qt::QueuedConnection);
}
