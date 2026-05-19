#include "file_scanner.h"
#include <QDirIterator>
#include <QImageReader>
#include <QElapsedTimer>
#include <QFileInfo>
#include <QDebug>

file_scanner::file_scanner(QObject* parent) : QObject(parent), stop_flag_(false) {}

file_scanner::~file_scanner() { stop_scan(); }

void file_scanner::start_scan(const QString& dir_path, int session_id)
{
    stop_flag_ = false;
    QElapsedTimer timer;
    timer.start();

    QStringList filters;
    filters << "*.jpg" << "*.jpeg" << "*.png" << "*.bmp" << "*.gif" << "*.webp";

    QDirIterator it(dir_path, filters, QDir::Files, QDirIterator::Subdirectories);
    QList<image_meta> scanned_images;
    int total_count = 0;

    while (it.hasNext())
    {
        if (stop_flag_)
        {
            break;
        }

        QString file_path = it.next();

        QImageReader reader(file_path);
        QSize size = reader.size();
        if (!size.isValid())
        {
            continue;
        }

        image_meta meta;
        meta.path = file_path;
        meta.original_size = size;

        scanned_images.append(meta);
        total_count++;
    }

    const qsizetype batch_size = 100;
    for (qsizetype i = 0; i < scanned_images.size(); i += batch_size)
    {
        QList<image_meta> batch;
        batch.reserve(static_cast<int>(batch_size));
        qsizetype end = std::min(i + batch_size, scanned_images.size());
        for (qsizetype j = i; j < end; ++j)
        {
            batch.append(scanned_images[j]);
        }
        emit images_scanned_batch(batch, session_id);
    }

    if (!stop_flag_)
    {
        emit scan_finished(total_count, timer.elapsed(), session_id);
    }
}

void file_scanner::stop_scan() { stop_flag_ = true; }
