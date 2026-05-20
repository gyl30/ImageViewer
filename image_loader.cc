#include <cmath>
#include <QCryptographicHash>
#include <QDebug>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QMetaObject>
#include <QImageReader>
#include <QStandardPaths>
#include "image_loader.h"

image_loader::image_loader(QObject* parent) : QObject(parent), abort_(false)
{
    cache_.setMaxCost(200L * 1024 * 1024);
    disk_cache_dir_ = QStandardPaths::writableLocation(QStandardPaths::CacheLocation) + "/thumbnails";
    QDir().mkpath(disk_cache_dir_);
    cleanup_disk_cache();
}

image_loader::~image_loader() { stop(); }

void image_loader::stop()
{
    abort_ = true;
    condition_.wakeAll();
}

QString image_loader::memory_cache_key(const load_task& task) const
{
    return QString("%1|%2x%3").arg(task.path).arg(task.target_size.width()).arg(task.target_size.height());
}

QString image_loader::disk_cache_path(const load_task& task) const
{
    QFileInfo file_info(task.path);
    const QString cache_seed =
        QString("%1|%2|%3|%4x%5")
            .arg(file_info.absoluteFilePath())
            .arg(file_info.lastModified().toMSecsSinceEpoch())
            .arg(file_info.size())
            .arg(task.target_size.width())
            .arg(task.target_size.height());
    const QByteArray hash = QCryptographicHash::hash(cache_seed.toUtf8(), QCryptographicHash::Sha1).toHex();
    return disk_cache_dir_ + "/" + QString::fromLatin1(hash) + ".png";
}

QImage image_loader::load_disk_cached_image(const load_task& task) const
{
    QImageReader reader(disk_cache_path(task));
    reader.setAutoTransform(true);
    return reader.read();
}

void image_loader::save_disk_cached_image(const load_task& task, const QImage& image)
{
    if (image.isNull())
    {
        return;
    }

    image.save(disk_cache_path(task), "PNG");
    cleanup_disk_cache();
}

void image_loader::cleanup_disk_cache()
{
    constexpr qint64 kMaxDiskCacheBytes = 512LL * 1024 * 1024;
    constexpr int kMaxDiskCacheFiles = 2000;

    QDir cache_dir(disk_cache_dir_);
    QFileInfoList files =
        cache_dir.entryInfoList(QStringList() << "*.png", QDir::Files, QDir::Time | QDir::Reversed);

    qint64 total_size = 0;
    for (const QFileInfo& file_info : files)
    {
        total_size += file_info.size();
    }

    while ((total_size > kMaxDiskCacheBytes || files.size() > kMaxDiskCacheFiles) && !files.isEmpty())
    {
        QFileInfo file_info = files.takeFirst();
        total_size -= file_info.size();
        QFile::remove(file_info.absoluteFilePath());
    }
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

        const QString cache_key = memory_cache_key(task);
        if (cache_.contains(cache_key))
        {
            emit thumbnail_loaded(task.id, task.path, *cache_.object(cache_key), task.session_id);
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

void image_loader::clear_cache()
{
    {
        QMutexLocker locker(&mutex_);
        cache_.clear();
    }

    QDir cache_dir(disk_cache_dir_);
    const QFileInfoList files = cache_dir.entryInfoList(QStringList() << "*.png", QDir::Files);
    for (const QFileInfo& file_info : files)
    {
        QFile::remove(file_info.absoluteFilePath());
    }
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
    const QString cache_key = memory_cache_key(current_task);

    {
        QMutexLocker locker(&mutex_);
        if (cache_.contains(cache_key))
        {
            emit thumbnail_loaded(current_task.id, current_task.path, *cache_.object(cache_key), current_task.session_id);
            return;
        }
    }

    QImage image = load_disk_cached_image(current_task);
    if (!image.isNull())
    {
        if (image.format() != QImage::Format_ARGB32_Premultiplied)
        {
            image = image.convertToFormat(QImage::Format_ARGB32_Premultiplied);
        }

        QMutexLocker locker(&mutex_);
        cache_.insert(cache_key, new QImage(image), image.sizeInBytes());
        locker.unlock();
        emit thumbnail_loaded(current_task.id, current_task.path, image, current_task.session_id);
        return;
    }

    QImageReader reader(current_task.path);
    reader.setAutoTransform(true);

    const QSize source_size = reader.size();
    const bool supports_scaled_size = reader.supportsOption(QImageIOHandler::ScaledSize);

    if (source_size.isValid())
    {
        const double estimated_mb =
            (static_cast<double>(source_size.width()) * source_size.height() * 4.0) / (1024.0 * 1024.0);
        const bool exceeds_allocation_limit = estimated_mb > kMaxImageAllocMB;

        if (supports_scaled_size && !current_task.target_size.isEmpty())
        {
            QSize scaled_size = current_task.target_size;
            if (exceeds_allocation_limit)
            {
                const double scale_factor = std::sqrt(kMaxImageAllocMB / estimated_mb);
                const QSize safe_size = (source_size * scale_factor).expandedTo(QSize(1, 1));
                scaled_size = scaled_size.boundedTo(safe_size).expandedTo(QSize(1, 1));
            }
            reader.setScaledSize(scaled_size);
        }
        else if (exceeds_allocation_limit)
        {
            return;
        }
    }

    image = reader.read();
    if (!image.isNull())
    {
        if (!current_task.target_size.isEmpty() && image.size() != current_task.target_size)
        {
            image = image.scaled(current_task.target_size, Qt::KeepAspectRatio, Qt::SmoothTransformation);
        }

        if (image.format() != QImage::Format_ARGB32_Premultiplied)
        {
            image = image.convertToFormat(QImage::Format_ARGB32_Premultiplied);
        }
        {
            QMutexLocker locker(&mutex_);
            auto cost = image.sizeInBytes();
            cache_.insert(cache_key, new QImage(image), cost);
        }

        save_disk_cached_image(current_task, image);

        emit thumbnail_loaded(current_task.id, current_task.path, image, current_task.session_id);
    }
}
