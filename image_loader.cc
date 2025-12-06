#include "image_loader.h"
#include <QImageReader>
#include <QDebug>
#include <QMetaObject>

image_loader::image_loader(QObject* parent) : QObject(parent) { cache_.setMaxCost(200L * 1024 * 1024); }

image_loader::~image_loader() = default;

void image_loader::request_thumbnail(quint64 id, const QString& path, const QSize& target_size, int session_id)
{
    if (pending_cancels_.contains(path))
    {
        pending_cancels_.remove(path);
    }

    if (cache_.contains(path))
    {
        emit thumbnail_loaded(id, path, *cache_.object(path), session_id);
        return;
    }

    task_queue_.prepend({id, path, target_size, session_id});

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

void image_loader::cancel_thumbnail(const QString& path) { pending_cancels_.insert(path); }

void image_loader::clear_all()
{
    task_queue_.clear();
    pending_cancels_.clear();
    is_processing_ = false;
}

void image_loader::process_next_task()
{
    load_task current_task;
    bool valid_task_found = false;

    while (!task_queue_.isEmpty())
    {
        current_task = task_queue_.takeFirst();

        if (pending_cancels_.contains(current_task.path))
        {
            pending_cancels_.remove(current_task.path);
            continue;
        }

        valid_task_found = true;
        break;
    }

    if (!valid_task_found)
    {
        is_processing_ = false;
        return;
    }

    if (cache_.contains(current_task.path))
    {
        emit thumbnail_loaded(current_task.id, current_task.path, *cache_.object(current_task.path), current_task.session_id);
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

        if (image.format() != QImage::Format_ARGB32_Premultiplied)
        {
            image.convertTo(QImage::Format_ARGB32_Premultiplied);
        }

        auto cost = image.sizeInBytes();
        cache_.insert(current_task.path, new QImage(image), cost);
        emit thumbnail_loaded(current_task.id, current_task.path, image, current_task.session_id);
    }

    QMetaObject::invokeMethod(this, "process_next_task", Qt::QueuedConnection);
}
