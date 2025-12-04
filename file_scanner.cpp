#include "file_scanner.h"
#include <QDirIterator>
#include <QImageReader>
#include <QElapsedTimer>
#include <QDebug>

file_scanner::file_scanner(QObject* parent) : QObject(parent), stop_flag_(false)
{
    qRegisterMetaType<std::vector<image_meta>>("std::vector<image_meta>");
}

file_scanner::~file_scanner() { stop_scan(); }

void file_scanner::stop_scan() { stop_flag_ = true; }

void file_scanner::start_scan(const QString& dir_path)
{
    stop_flag_ = false;
    QElapsedTimer timer;
    timer.start();

    std::vector<image_meta> batch_buffer;
    batch_buffer.reserve(100);

    int total_scanned = 0;
    const int kBatchSize = 100;

    QDirIterator it(dir_path, QStringList() << "*.jpg" << "*.png" << "*.jpeg" << "*.webp" << "*.bmp", QDir::Files, QDirIterator::Subdirectories);

    while (it.hasNext())
    {
        if (stop_flag_)
        {
            break;
        }

        QString file_path = it.next();

        QImageReader reader(file_path);
        QSize size = reader.size();

        if (size.isValid())
        {
            image_meta meta;
            meta.path = file_path;
            meta.original_size = size;

            batch_buffer.push_back(meta);
            total_scanned++;
        }

        if (batch_buffer.size() >= kBatchSize)
        {
            emit images_scanned_batch(batch_buffer);
            batch_buffer.clear();
            batch_buffer.reserve(kBatchSize);
        }
    }

    if (!batch_buffer.empty() && !stop_flag_)
    {
        emit images_scanned_batch(batch_buffer);
    }

    if (!stop_flag_)
    {
        emit scan_finished(total_scanned, timer.elapsed());
    }
}
