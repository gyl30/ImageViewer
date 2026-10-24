#include <QApplication>
#include <QDateTime>
#include <QDirIterator>
#include <QFile>
#include <QGraphicsPixmapItem>
#include <QImageReader>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLocale>
#include <QMimeData>
#include <QPainter>
#include <QScrollBar>
#include <QStandardPaths>
#include <QtTest>
#include <atomic>
#include <sys/resource.h>
#include <time.h>
#include "main_window.h"
#include "image_loader.h"
#include "image_viewer_window.h"
#include "waterfall_scene.h"
#include "waterfall_view.h"
#include "waterfall_item.h"

static double process_cpu_ms()
{
    rusage usage{};
    getrusage(RUSAGE_SELF, &usage);
    return 1000.0 * (usage.ru_utime.tv_sec + usage.ru_stime.tv_sec) +
           (usage.ru_utime.tv_usec + usage.ru_stime.tv_usec) / 1000.0;
}

static double thread_cpu_ms()
{
    timespec value{};
    clock_gettime(CLOCK_THREAD_CPUTIME_ID, &value);
    return value.tv_sec * 1000.0 + value.tv_nsec / 1000000.0;
}

static qint64 proc_value(const QString& path, const QByteArray& key)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
    {
        return -1;
    }
    for (const auto& line : file.readAll().split('\n'))
    {
        if (line.startsWith(key + ':'))
        {
            return line.mid(key.size() + 1).trimmed().split(' ').first().toLongLong();
        }
    }
    return -1;
}

class performance_probe
{
   public:
    explicit performance_probe(main_window& window) : window_(window)
    {
        timer_.start();
        QObject::connect(window_.scene_, &waterfall_scene::request_load_batch, &window_, [this]()
        {
            QMutexLocker locker(&window_.image_loader_->mutex_);
            max_queue_ = std::max(max_queue_, window_.image_loader_->task_queue_.size());
            max_active_ = std::max(max_active_, window_.scene_->active_items_.size());
        });
        QObject::connect(window_.file_scanner_, &file_scanner::images_scanned_batch, &window_, [this]()
        {
            if (first_batch_ms_ < 0)
            {
                first_batch_ms_ = timer_.elapsed();
            }
        });
        QObject::connect(window_.file_scanner_, &file_scanner::scan_finished, &window_, [this](int, qint64 ms)
        {
            scan_ms_ = ms;
            scan_done_ = true;
        });
        QObject::connect(window_.file_scanner_, &file_scanner::scan_finished, window_.file_scanner_, [this]()
        {
            scan_cpu_ms_ = thread_cpu_ms();
        }, Qt::DirectConnection);
        QObject::connect(window_.image_loader_, &image_loader::thumbnail_loaded, window_.image_loader_, [this]()
        {
            loader_cpu_ms_ = thread_cpu_ms();
        }, Qt::DirectConnection);
        QObject::connect(window_.image_loader_, &image_loader::thumbnail_loaded, &window_,
                         [this](quint64 id, const QString&, const QImage& image)
        {
            ++decoded_count_;
            if (!image.isNull() && first_thumbnail_ms_ < 0)
            {
                first_thumbnail_ms_ = timer_.elapsed();
            }
            if (!image.isNull() && first_visible_ms_ < 0)
            {
                const QRectF visible = window_.view_->mapToScene(window_.view_->viewport()->rect()).boundingRect();
                for (auto* item : window_.scene_->active_items_)
                {
                    if (item->get_request_id() == id && item->sceneBoundingRect().intersects(visible))
                    {
                        first_visible_ms_ = timer_.elapsed();
                        break;
                    }
                }
            }
        });
    }

    bool wait_idle(int timeout = 120000)
    {
        QElapsedTimer wait;
        wait.start();
        while (wait.elapsed() < timeout)
        {
            QTest::qWait(20);
            QMutexLocker locker(&window_.image_loader_->mutex_);
            if (scan_done_ && !window_.scene_->is_laying_out_ && !window_.image_loader_->is_loading_)
            {
                return true;
            }
        }
        return false;
    }

    void sample(const QString& phase)
    {
        auto* scene = window_.scene_;
        auto* loader = window_.image_loader_;
        QJsonObject row{{"phase", phase}, {"elapsed_ms", timer_.elapsed()}, {"cpu_ms", process_cpu_ms()},
                        {"rss_kib", proc_value("/proc/self/status", "VmRSS")},
                        {"peak_rss_kib", proc_value("/proc/self/status", "VmHWM")},
                        {"pss_kib", proc_value("/proc/self/smaps_rollup", "Pss")},
                        {"model_count", static_cast<qint64>(scene->all_models_.size())},
                        {"metadata_capacity_bytes", static_cast<qint64>(scene->all_models_.capacity() * sizeof(layout_model) +
                            scene->item_bottom_index_.capacity() * sizeof(int) + scene->image_paths_.capacity() * sizeof(QString))},
                        {"active_items", scene->active_items_.size()}, {"pool_items", scene->pool_.size()},
                        {"total_items", scene->items().size()}, {"loaded_stat_paths", 0},
                        {"decoded_count", decoded_count_}, {"scan_cpu_ms", scan_cpu_ms_.load()},
                        {"loader_cpu_ms", loader_cpu_ms_.load()}};
        qint64 path_bytes = 0;
        for (const auto& path : scene->image_paths_)
        {
            path_bytes += (path.size() + 1) * sizeof(QChar);
        }
        row["metadata_path_bytes"] = path_bytes;
        qint64 pixel_bytes = 0;
        for (auto* item : scene->active_items_)
        {
            const auto pixmap = item->pixmap();
            pixel_bytes += static_cast<qint64>(pixmap.width()) * pixmap.height() * pixmap.depth() / 8;
        }
        row["active_pixmap_logical_bytes"] = pixel_bytes;
        {
            QMutexLocker locker(&loader->mutex_);
            row["queue_tasks"] = loader->task_queue_.size();
            row["cache_logical_bytes"] = loader->cache_.totalCost();
            row["cache_budget_bytes"] = loader->cache_.maxCost();
        }
        for (const QByteArray& field : {"rchar", "wchar", "syscr", "syscw", "read_bytes", "write_bytes"})
        {
            row[QString::fromLatin1(field)] = proc_value("/proc/self/io", field);
        }
        qint64 cache_bytes = 0;
        int cache_files = 0;
        QDirIterator files(loader->disk_cache_dir_, {"*.png"}, QDir::Files);
        while (files.hasNext())
        {
            files.next();
            cache_bytes += files.fileInfo().size();
            ++cache_files;
        }
        row["disk_cache_bytes"] = cache_bytes;
        row["disk_cache_files"] = cache_files;
        samples_.append(row);
    }

    int run(const QString& directory, const QString& output, bool clear_disk)
    {
        sample("empty");
        const double idle_start = process_cpu_ms();
        QTest::qWait(500);
        idle_cpu_ms_ = process_cpu_ms() - idle_start;
        timer_.restart();
        QMimeData mime;
        mime.setUrls({QUrl::fromLocalFile(directory)});
        QDragEnterEvent enter(QPoint(1, 1), Qt::CopyAction, &mime, Qt::LeftButton, Qt::NoModifier);
        QApplication::sendEvent(&window_, &enter);
        QDropEvent drop(QPointF(1, 1), Qt::CopyAction, &mime, Qt::LeftButton, Qt::NoModifier);
        QApplication::sendEvent(&window_, &drop);
        if (!drop.isAccepted() || !wait_idle())
        {
            return 2;
        }
        QTest::qWait(200);
        sample("ready");
        QImage surface(window_.view_->size(), QImage::Format_RGB32);
        double render_ms = 0;
        auto* scroll = window_.view_->verticalScrollBar();
        for (int lap = 0; lap < 3; ++lap)
        {
            for (int step = 0; step < 120; ++step)
            {
                const int position = lap % 2 == 0 ? step : 119 - step;
                scroll->setValue(static_cast<qint64>(scroll->maximum()) * position / 119);
                window_.view_->check_visible_area();
                QTest::qWait(40);
                QElapsedTimer render;
                render.start();
                QPainter painter(&surface);
                window_.view_->render(&painter);
                render_ms += render.nsecsElapsed() / 1000000.0;
            }
            if (!wait_idle())
            {
                return 3;
            }
            sample(QString("scroll_%1").arg(lap + 1));
        }
        for (int width : {1100, 1400, 1280})
        {
            window_.resize(width, 800);
            QTest::qWait(180);
            if (!wait_idle())
            {
                return 4;
            }
        }
        scroll->setValue(0);
        QTest::qWait(180);
        if (!wait_idle())
        {
            return 4;
        }
        sample("resized");
        window_.scene_->clear_items();
        if (clear_disk)
        {
            QMetaObject::invokeMethod(window_.image_loader_, &image_loader::clear_cache, Qt::QueuedConnection);
        }
        else
        {
            QMutexLocker locker(&window_.image_loader_->mutex_);
            window_.image_loader_->cache_.clear();
        }
        if (!wait_idle())
        {
            return 5;
        }
        sample("cleared");
        QJsonObject result{{"qt", qVersion()}, {"dataset", directory}, {"viewport", "1280x800 window"},
                           {"first_batch_ms", first_batch_ms_}, {"first_thumbnail_ms", first_thumbnail_ms_},
                           {"first_visible_ms", first_visible_ms_},
                           {"scan_ms", scan_ms_}, {"idle_cpu_ms_500ms", idle_cpu_ms_},
                           {"max_queue_sampled", max_queue_}, {"max_active_items", max_active_},
                           {"clear_disk_at_end", clear_disk},
                           {"explicit_render_ms_360_frames", render_ms}, {"samples", samples_}};
        QFile file(output);
        if (!file.open(QIODevice::WriteOnly))
        {
            return 6;
        }
        file.write(QJsonDocument(result).toJson());
        return 0;
    }

   private:
    main_window& window_;
    QElapsedTimer timer_;
    bool scan_done_ = false;
    qint64 first_batch_ms_ = -1;
    qint64 first_thumbnail_ms_ = -1;
    qint64 first_visible_ms_ = -1;
    qint64 scan_ms_ = 0;
    int decoded_count_ = 0;
    double idle_cpu_ms_ = 0;
    std::atomic<double> loader_cpu_ms_{0};
    std::atomic<double> scan_cpu_ms_{0};
    QJsonArray samples_;
    qsizetype max_queue_ = 0;
    qsizetype max_active_ = 0;
};

int main(int argc, char** argv)
{
    QApplication app(argc, argv);
    if (argc != 4)
    {
        return 1;
    }
    QLocale::setDefault(QLocale(QLocale::English, QLocale::UnitedStates));
    QImageReader::setAllocationLimit(kQtImageReaderAllocMB);
    main_window window;
    window.resize(1280, 800);
    window.show();
    QTest::qWait(200);
    performance_probe probe(window);
    return probe.run(QString::fromLocal8Bit(argv[1]), QString::fromLocal8Bit(argv[2]), QString::fromLocal8Bit(argv[3]) == "warm");
}
