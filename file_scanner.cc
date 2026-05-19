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
constexpr qsizetype kEmitBatchSize = 500;

struct scanned_image
{
    QString path;
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

    while (it.hasNext())
    {
        if (stop_flag_)
        {
            break;
        }

        QString file_path = it.next();
        scanned_images.append({file_path, QFileInfo(file_path).fileName()});
    }

    if (stop_flag_)
    {
        return;
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
                  return left.path < right.path;
              });

    int total_count = 0;
    QList<image_meta> batch;
    batch.reserve(static_cast<int>(kEmitBatchSize));

    for (qsizetype i = 0; i < scanned_images.size(); i += kEmitBatchSize)
    {
        if (stop_flag_)
        {
            return;
        }

        qsizetype end = std::min(i + kEmitBatchSize, scanned_images.size());
        for (qsizetype j = i; j < end; ++j)
        {
            QImageReader reader(scanned_images[j].path);
            QSize size = reader.size();
            if (!size.isValid())
            {
                continue;
            }

            batch.append({scanned_images[j].path, size});
            total_count++;
        }

        if (!batch.isEmpty())
        {
            emit images_scanned_batch(batch, session_id);
            batch.clear();
        }
    }

    if (!stop_flag_)
    {
        emit scan_finished(total_count, timer.elapsed(), session_id);
    }
}

void file_scanner::stop_scan() { stop_flag_ = true; }
