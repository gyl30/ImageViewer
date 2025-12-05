#include <cmath>
#include <QDebug>
#include <QImageReader>
#include "image_loader.h"

image_loader::image_loader(QObject* parent) : QObject(parent), stop_flag_(false) { cache_.setMaxCost(200L * 1024 * 1024); }

image_loader::~image_loader() { stop_processing(); }

void image_loader::start_processing() { process_loop(); }

void image_loader::stop_processing()
{
    stop_flag_ = true;
    condition_.wakeAll();
}

void image_loader::request_thumbnail(const QString& path, const QSize& target_size)
{
    QMutexLocker locker(&mutex_);

    if (cache_.contains(path))
    {
        emit thumbnail_loaded(path, *cache_.object(path));
        return;
    }

    task_queue_.prepend({path, target_size});

    pending_paths_.insert(path);

    condition_.wakeOne();
}

void image_loader::cancel_thumbnail(const QString& path)
{
    QMutexLocker locker(&mutex_);

    if (pending_paths_.contains(path))
    {
        pending_paths_.remove(path);
    }
}

void image_loader::process_loop()
{
    while (!stop_flag_)
    {
        load_task current_task;

        {
            QMutexLocker locker(&mutex_);
            while (task_queue_.isEmpty() && !stop_flag_)
            {
                condition_.wait(&mutex_);
            }
            if (stop_flag_)
            {
                break;
            }

            current_task = task_queue_.takeFirst();

            if (!pending_paths_.contains(current_task.path))
            {
                continue;
            }

            pending_paths_.remove(current_task.path);

            current_loading_path_ = current_task.path;
        }

        {
            QMutexLocker locker(&mutex_);
            if (cache_.contains(current_task.path))
            {
                emit thumbnail_loaded(current_task.path, *cache_.object(current_task.path));
                continue;
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
            if (image.width() != current_task.target_size.width() && !current_task.target_size.isEmpty())
            {
                image = image.scaled(current_task.target_size, Qt::KeepAspectRatio, Qt::SmoothTransformation);
            }

            auto cost = image.sizeInBytes();

            {
                QMutexLocker locker(&mutex_);
                cache_.insert(current_task.path, new QImage(image), cost);
            }

            emit thumbnail_loaded(current_task.path, image);
        }
    }
}
