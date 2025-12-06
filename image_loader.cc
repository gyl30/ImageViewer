#include "image_loader.h"
#include <QImageReader>
#include <QDebug>
#include <QMetaObject>

image_loader::image_loader(QObject* parent) : QObject(parent) { cache_.setMaxCost(200L * 1024 * 1024); }

image_loader::~image_loader() = default;

void image_loader::request_thumbnail(quint64 id, const QString& path, const QSize& target_size)
{
    if (cache_.contains(path))
    {
        emit thumbnail_loaded(id, path, *cache_.object(path));
        return;
    }

    task_queue_.prepend({id, path, target_size});

    if (task_queue_.size() > 200)
    {
        task_queue_.removeLast();
    }

    if (!is_processing_)
    {
        is_processing_ = true;
        process_next_task();
    }
}

void image_loader::cancel_thumbnail(const QString& path)
{
    if (task_queue_.isEmpty())
    {
        return;
    }

    auto it = std::remove_if(task_queue_.begin(), task_queue_.end(), [&path](const load_task& task) { return task.path == path; });
    task_queue_.erase(it, task_queue_.end());
}

void image_loader::clear_all()
{
    task_queue_.clear();
    is_processing_ = false;
}

void image_loader::process_next_task()
{
    if (task_queue_.isEmpty())
    {
        is_processing_ = false;
        return;
    }

    load_task current_task = task_queue_.takeFirst();

    if (cache_.contains(current_task.path))
    {
        emit thumbnail_loaded(current_task.id, current_task.path, *cache_.object(current_task.path));

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

        emit thumbnail_loaded(current_task.id, current_task.path, image);
    }

    QMetaObject::invokeMethod(this, "process_next_task", Qt::QueuedConnection);
}
