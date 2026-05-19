#include "file_scanner.h"
#include <algorithm>
#include <QCollator>
#include <QDirIterator>
#include <QImageReader>
#include <QElapsedTimer>
#include <QFileInfo>
#include <QDateTime>
#include <QDebug>

namespace
{
constexpr qsizetype kEmitBatchSize = 500;
constexpr int kSortByFileName = 0;
constexpr int kSortByModifiedTime = 1;
constexpr int kSortByFileSize = 2;

struct scanned_image
{
    QString path;
    QString file_name;
    qint64 modified_time = 0;
    qint64 file_size = 0;
};
}

file_scanner::file_scanner(QObject* parent) : QObject(parent), stop_flag_(false) {}

file_scanner::~file_scanner() { stop_scan(); }

void file_scanner::start_scan(const QString& dir_path, int session_id, int sort_mode, bool descending)
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
        QFileInfo file_info(file_path);
        scanned_images.append(
            {file_path, file_info.fileName(), file_info.lastModified().toMSecsSinceEpoch(), file_info.size()});
    }

    if (stop_flag_)
    {
        return;
    }

    QCollator collator;
    collator.setNumericMode(true);
    collator.setCaseSensitivity(Qt::CaseInsensitive);

    const auto compare_file_name =
        [&collator](const scanned_image& left, const scanned_image& right)
    {
        int cmp = collator.compare(left.file_name, right.file_name);
        if (cmp != 0)
        {
            return cmp;
        }
        return QString::compare(left.path, right.path, Qt::CaseInsensitive);
    };

    std::sort(scanned_images.begin(),
              scanned_images.end(),
              [sort_mode, descending, &compare_file_name](const scanned_image& left, const scanned_image& right)
              {
                  int cmp = 0;
                  if (sort_mode == kSortByModifiedTime && left.modified_time != right.modified_time)
                  {
                      cmp = left.modified_time < right.modified_time ? -1 : 1;
                  }
                  else if (sort_mode == kSortByFileSize && left.file_size != right.file_size)
                  {
                      cmp = left.file_size < right.file_size ? -1 : 1;
                  }
                  else
                  {
                      cmp = compare_file_name(left, right);
                  }

                  return descending ? cmp > 0 : cmp < 0;
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
