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

    QList<image_meta> batch;
    batch.reserve(100);
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

        batch.append(meta);
        total_count++;

        if (batch.size() >= 100)
        {
            emit images_scanned_batch(batch, session_id);
            batch.clear();
            batch.reserve(100);
        }
    }

    if (!batch.isEmpty())
    {
        emit images_scanned_batch(batch, session_id);
    }

    if (!stop_flag_)
    {
        emit scan_finished(total_count, timer.elapsed(), session_id);
    }
}

void file_scanner::stop_scan() { stop_flag_ = true; }
