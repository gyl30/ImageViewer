#include <QDebug>
#include <QMetaObject>
#include <QImageReader>
#include "image_loader.h"

image_loader::image_loader(QObject* parent) : QObject(parent), abort_(false) { cache_.setMaxCost(200L * 1024 * 1024); }

image_loader::~image_loader() { stop(); }

void image_loader::stop()
{
    abort_ = true;
    condition_.wakeAll();
}

void image_loader::request_thumbnails(const QList<load_task>& tasks)
{
    QMutexLocker locker(&mutex_);
    bool has_new_tasks = false;
    for (const auto& task : tasks)
    {
        if (pending_cancels_.contains(task.path))
        {
            pending_cancels_.remove(task.path);
        }

        if (cache_.contains(task.path))
        {
            emit thumbnail_loaded(task.id, task.path, *cache_.object(task.path), task.session_id);
            continue;
        }

        task_queue_.prepend(task);
        has_new_tasks = true;
    }

    QList<QString> dropped_paths;
    while (task_queue_.size() > 200)
    {
        load_task task = task_queue_.takeLast();
        dropped_paths.append(task.path);
    }

    if (!dropped_paths.isEmpty())
    {
        locker.unlock();
        emit tasks_dropped(dropped_paths);
        locker.relock();
    }

    if (has_new_tasks)
    {
        condition_.wakeOne();
    }
}

void image_loader::cancel_thumbnails(const QList<QString>& paths)
{
    QMutexLocker locker(&mutex_);
    for (const auto& path : paths)
    {
        pending_cancels_.insert(path);
    }
}

void image_loader::clear_all()
{
    QMutexLocker locker(&mutex_);
    task_queue_.clear();
    pending_cancels_.clear();
}

void image_loader::start_loop()
{
    while (!abort_)
    {
        load_task current_task;
        bool valid_task_found = false;

        {
            QMutexLocker locker(&mutex_);

            while (task_queue_.isEmpty() && !abort_)
            {
                condition_.wait(&mutex_);
            }

            if (abort_)
            {
                return;
            }

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
        }

        if (valid_task_found)
        {
            load_image_internal(current_task);
        }
    }
}

void image_loader::load_image_internal(const load_task& current_task)
{
    {
        QMutexLocker locker(&mutex_);
        if (cache_.contains(current_task.path))
        {
            emit thumbnail_loaded(current_task.id, current_task.path, *cache_.object(current_task.path), current_task.session_id);
            return;
        }
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
        {
            QMutexLocker locker(&mutex_);
            auto cost = image.sizeInBytes();
            cache_.insert(current_task.path, new QImage(image), cost);
        }

        emit thumbnail_loaded(current_task.id, current_task.path, image, current_task.session_id);
    }
}
