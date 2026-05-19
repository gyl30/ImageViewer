#include "file_scanner.h"
#include <algorithm>
#include <QCollator>
#include <QDirIterator>
#include <QImageReader>
#include <QElapsedTimer>
#include <QFileInfo>
#include <QDebug>

namespace
{
struct scanned_image
{
    image_meta meta;
    QString file_name;
};
}

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
    QList<scanned_image> scanned_images;
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

        scanned_images.append({{file_path, size}, QFileInfo(file_path).fileName()});
        total_count++;
    }

    QCollator collator;
    collator.setNumericMode(true);
    collator.setCaseSensitivity(Qt::CaseInsensitive);

    std::sort(scanned_images.begin(),
              scanned_images.end(),
              [&collator](const scanned_image& left, const scanned_image& right)
              {
                  int cmp = collator.compare(left.file_name, right.file_name);
                  if (cmp != 0)
                  {
                      return cmp < 0;
                  }
                  return left.meta.path < right.meta.path;
              });

    const qsizetype batch_size = 100;
    for (qsizetype i = 0; i < scanned_images.size(); i += batch_size)
    {
        QList<image_meta> batch;
        batch.reserve(static_cast<int>(batch_size));
        qsizetype end = std::min(i + batch_size, scanned_images.size());
        for (qsizetype j = i; j < end; ++j)
        {
            batch.append(scanned_images[j].meta);
        }
        emit images_scanned_batch(batch, session_id);
    }

    if (!stop_flag_)
    {
        emit scan_finished(total_count, timer.elapsed(), session_id);
    }
}

void file_scanner::stop_scan() { stop_flag_ = true; }
