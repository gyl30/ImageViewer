#include <cmath>
#include <algorithm>
#include <QCryptographicHash>
#include <QDebug>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QMetaObject>
#include <QImageReader>
#include <QImageWriter>
#include <QStandardPaths>
#include "image_loader.h"

namespace
{
constexpr int kThumbnailCacheVersion = 2;
constexpr qint64 kMaxDiskCacheBytes = 128LL * 1024 * 1024;
constexpr int kMaxDiskCacheFiles = 1000;
constexpr int kCacheRetainPercent = 80;
constexpr int kPngCompression = 15;
}

image_loader::image_loader(QObject* parent) : QObject(parent), abort_(false)
{
    cache_.setMaxCost(kThumbnailCacheMiB * 1024 * 1024);
    disk_cache_dir_ = QStandardPaths::writableLocation(QStandardPaths::CacheLocation) + "/thumbnails";
    QDir().mkpath(disk_cache_dir_);
    cleanup_disk_cache();
}

image_loader::~image_loader() { stop(); }

void image_loader::stop()
{
    abort_ = true;
}

QString image_loader::disk_cache_path(const load_task& task) const
{
    QFileInfo file_info(task.path);
    const QString cache_seed =
        QString("%1|%2|%3|%4|%5x%6")
            .arg(kThumbnailCacheVersion)
            .arg(file_info.absoluteFilePath())
            .arg(file_info.lastModified().toMSecsSinceEpoch())
            .arg(file_info.size())
            .arg(task.target_size.width())
            .arg(task.target_size.height());
    const QByteArray hash = QCryptographicHash::hash(cache_seed.toUtf8(), QCryptographicHash::Sha1).toHex();
    return disk_cache_dir_ + "/" + QString::fromLatin1(hash) + ".png";
}

void image_loader::cleanup_disk_cache()
{
    QDir cache_dir(disk_cache_dir_);
    QFileInfoList files =
        cache_dir.entryInfoList(QStringList() << "*.png", QDir::Files, QDir::Time | QDir::Reversed);

    qint64 total_size = 0;
    for (const QFileInfo& file_info : files)
    {
        total_size += file_info.size();
    }

    const bool exceeds_budget = total_size > kMaxDiskCacheBytes || files.size() > kMaxDiskCacheFiles;
    while (exceeds_budget && !files.isEmpty() &&
           (total_size > kMaxDiskCacheBytes * kCacheRetainPercent / 100 ||
            files.size() > kMaxDiskCacheFiles * kCacheRetainPercent / 100))
    {
        QFileInfo file_info = files.takeFirst();
        total_size -= file_info.size();
        QFile::remove(file_info.absoluteFilePath());
    }
    disk_cache_bytes_ = total_size;
    disk_cache_files_ = files.size();
}

void image_loader::request_thumbnails(const QList<load_task>& tasks)
{
    QMutexLocker locker(&mutex_);
    for (const auto& task : tasks)
    {
        if (task_queue_.size() >= kMaxThumbnailTasks)
        {
            break;
        }
        task_queue_.append(task);
    }

    if (!task_queue_.isEmpty() && !is_loading_ && !abort_)
    {
        is_loading_ = true;
        QMetaObject::invokeMethod(this, &image_loader::load_next, Qt::QueuedConnection);
    }
}

void image_loader::cancel_thumbnails(const QList<QString>& paths)
{
    QMutexLocker locker(&mutex_);
    task_queue_.removeIf([&paths](const load_task& task) { return paths.contains(task.path); });
}

void image_loader::prioritize_thumbnails(const QList<quint64>& ids)
{
    QMutexLocker locker(&mutex_);
    const auto rank = [&ids](quint64 id)
    {
        const qsizetype index = ids.indexOf(id);
        return index < 0 ? ids.size() : index;
    };
    std::sort(task_queue_.begin(), task_queue_.end(), [&](const load_task& left, const load_task& right)
    {
        return rank(left.id) < rank(right.id);
    });
}

void image_loader::clear_all()
{
    QMutexLocker locker(&mutex_);
    task_queue_.clear();
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
    disk_cache_bytes_ = 0;
    disk_cache_files_ = 0;
}

void image_loader::load_next()
{
    load_task current_task;
    {
        QMutexLocker locker(&mutex_);
        if (abort_ || task_queue_.isEmpty())
        {
            is_loading_ = false;
            return;
        }
        current_task = task_queue_.takeFirst();
    }

    load_image_internal(current_task);
    QMetaObject::invokeMethod(this, &image_loader::load_next, Qt::QueuedConnection);
}

void image_loader::load_image_internal(const load_task& current_task)
{
    const QString cache_key = disk_cache_path(current_task);

    {
        QMutexLocker locker(&mutex_);
        if (cache_.contains(cache_key))
        {
            emit thumbnail_loaded(current_task.id, current_task.path, *cache_.object(cache_key), current_task.session_id);
            return;
        }
    }

    QImage image(cache_key);
    if (!image.isNull())
    {
        const auto format = image.hasAlphaChannel() ? QImage::Format_ARGB32_Premultiplied : QImage::Format_RGB32;
        if (image.format() != format)
        {
            image = image.convertToFormat(format);
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
            if (reader.transformation().testFlag(QImageIOHandler::TransformationRotate90))
            {
                scaled_size.transpose();
            }
            scaled_size = source_size.scaled(scaled_size, Qt::KeepAspectRatio).expandedTo(QSize(1, 1));
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
            emit thumbnail_loaded(current_task.id, current_task.path, QImage(), current_task.session_id);
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

        const auto format = image.hasAlphaChannel() ? QImage::Format_ARGB32_Premultiplied : QImage::Format_RGB32;
        if (image.format() != format)
        {
            image = image.convertToFormat(format);
        }
        {
            QMutexLocker locker(&mutex_);
            auto cost = image.sizeInBytes();
            cache_.insert(cache_key, new QImage(image), cost);
        }

        emit thumbnail_loaded(current_task.id, current_task.path, image, current_task.session_id);
        const QFileInfo previous(cache_key);
        const bool existed = previous.exists();
        const qint64 previous_size = previous.size();
        QImageWriter writer(cache_key, "PNG");
        writer.setCompression(kPngCompression);
        if (writer.write(image))
        {
            disk_cache_bytes_ += QFileInfo(cache_key).size() - previous_size;
            disk_cache_files_ += existed ? 0 : 1;
            if (disk_cache_bytes_ > kMaxDiskCacheBytes || disk_cache_files_ > kMaxDiskCacheFiles)
            {
                cleanup_disk_cache();
            }
        }
        return;
    }
    emit thumbnail_loaded(current_task.id, current_task.path, image, current_task.session_id);
}
