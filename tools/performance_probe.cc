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
#include <QMovie>
#include <QImageWriter>
#include <QBuffer>
#include <QTemporaryDir>
#include <QAction>
#include <QScrollBar>
#include <QStandardPaths>
#include <QtTest>
#include <atomic>
#include <sys/resource.h>
#include <time.h>
#include <fcntl.h>
#ifdef __GLIBC__
#include <malloc.h>
#endif
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
    enum class run_mode { cold, warm, capture, trim, soak };
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
        QDirIterator files(loader->disk_cache_dir_, QDir::Files);
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

    int run(const QString& directory, const QString& output, run_mode mode)
    {
        const bool clear_disk = mode == run_mode::warm || mode == run_mode::soak;
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
        if (mode == run_mode::capture)
        {
            QDir().mkpath(output);
            if (window_.scene_->active_items_.contains(0))
            {
                window_.scene_->active_items_[0]->setSelected(true);
            }
            window_.grab().save(output + "/light.png");
            window_.dark_theme_action_->setChecked(true);
            QTest::qWait(40);
            window_.grab().save(output + "/dark.png");
            window_.show_image_viewer(window_.scene_->image_paths_.first(), window_.scene_->get_all_paths());
            QElapsedTimer wait;
            wait.start();
            while (!window_.viewer_window_->windowTitle().startsWith("Viewer -") && wait.elapsed() < 10000)
            {
                QTest::qWait(20);
            }
            window_.viewer_window_->grab().save(output + "/preview.png");
            window_.viewer_window_->close();
            window_.dark_theme_action_->setChecked(false);
            QTemporaryDir empty;
            window_.open_path(empty.path(), false);
            QTest::qWait(100);
            window_.grab().save(output + "/empty.png");
            return 0;
        }
        QImage surface(window_.view_->size(), QImage::Format_RGB32);
        double render_ms = 0;
        auto* scroll = window_.view_->verticalScrollBar();
        const int laps = mode == run_mode::soak ? 30 : 3;
        for (int lap = 0; lap < laps; ++lap)
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
            QMetaObject::invokeMethod(window_.image_loader_, &image_loader::clear_cache, Qt::BlockingQueuedConnection);
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
        if (mode == run_mode::trim)
        {
#ifdef __GLIBC__
            malloc_trim(0);
#endif
            sample("trimmed");
        }
        QJsonObject result{{"qt", qVersion()}, {"dataset", directory}, {"viewport", "1280x800 window"},
                           {"first_batch_ms", first_batch_ms_}, {"first_thumbnail_ms", first_thumbnail_ms_},
                           {"first_visible_ms", first_visible_ms_},
                           {"scan_ms", scan_ms_}, {"idle_cpu_ms_500ms", idle_cpu_ms_},
                           {"max_queue_sampled", max_queue_}, {"max_active_items", max_active_},
                           {"clear_disk_at_end", clear_disk},
                           {QString("explicit_render_ms_%1_frames").arg(laps * 120), render_ms}, {"samples", samples_}};
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

static int viewer_benchmark(const QString& directory, const QString& output)
{
    QJsonArray samples;
    const auto sample = [&samples](const QString& phase)
    {
        samples.append(QJsonObject{{"phase", phase}, {"cpu_ms", process_cpu_ms()},
            {"rss_kib", proc_value("/proc/self/status", "VmRSS")},
            {"pss_kib", proc_value("/proc/self/smaps_rollup", "Pss")},
            {"peak_rss_kib", proc_value("/proc/self/status", "VmHWM")}});
    };
    auto* viewer = new image_viewer_window();
    viewer->show();
    sample("empty");
    QStringList paths;
    for (int index = 0; index < 3; ++index)
    {
        paths.append(directory + QString("/large-%1.jpg").arg(index));
    }
    viewer->set_image_list(paths);
    QElapsedTimer elapsed;
    elapsed.start();
    viewer->set_image_path(paths[1]);
    while (!viewer->windowTitle().startsWith("Viewer -") && elapsed.elapsed() < 30000)
    {
        QTest::qWait(10);
    }
    const qint64 first_display_ms = elapsed.elapsed();
    QThreadPool::globalInstance()->waitForDone();
    QTest::qWait(500);
    QThreadPool::globalInstance()->waitForDone();
    QTest::qWait(500);
    sample("large_and_neighbors");
    viewer->set_image_path(directory + "/large-extra.jpg");
    elapsed.restart();
    viewer->close();
    const qint64 close_ms = elapsed.elapsed();
    QTest::qWait(500);
    sample("closed");
    delete viewer;
    sample("destroyed");
    viewer = new image_viewer_window();
    viewer->show();
    viewer->set_image_path(directory + "/animation.gif");
    QTest::qWait(3000);
    auto* movie = viewer->findChild<QMovie*>();
    const int frames = movie == nullptr ? 0 : movie->frameCount();
    const bool cache_all = movie != nullptr && movie->cacheMode() == QMovie::CacheAll;
    sample("gif_3_seconds");
    viewer->close();
    QTest::qWait(50);
    delete viewer;
    sample("gif_destroyed");
    QFile file(output);
    if (!file.open(QIODevice::WriteOnly))
    {
        return 6;
    }
    file.write(QJsonDocument(QJsonObject{{"first_display_ms", first_display_ms}, {"close_ms", close_ms},
        {"gif_frames", frames}, {"gif_cache_all", cache_all}, {"samples", samples}}).toJson());
    return 0;
}

static int encoding_benchmark(const QString& source, const QString& output)
{
    const QImage original = QImage(source).scaledToWidth(256, Qt::SmoothTransformation);
    if (original.isNull())
    {
        return 2;
    }
    QJsonArray rows;
    struct encoding_option
    {
        QImage::Format format;
        QByteArray codec;
        int compression;
        int quality;
    };
    const encoding_option options[] = {
        {QImage::Format_ARGB32_Premultiplied, "PNG", 50, -1},
        {QImage::Format_ARGB32_Premultiplied, "PNG", 15, -1},
        {QImage::Format_RGB32, "PNG", 50, -1},
        {QImage::Format_RGB32, "PNG", 15, -1},
        {QImage::Format_RGB32, "JPEG", -1, 95}};
    for (const auto& option : options)
    {
        const QImage image = original.convertToFormat(option.format);
        const double start = process_cpu_ms();
        QByteArray bytes;
        for (int index = 0; index < 100; ++index)
        {
            bytes.clear();
            QBuffer buffer(&bytes);
            buffer.open(QIODevice::WriteOnly);
            QImageWriter writer(&buffer, option.codec);
            writer.setCompression(option.compression);
            writer.setQuality(option.quality);
            if (!writer.write(image))
            {
                return 3;
            }
        }
        const double encode_cpu = process_cpu_ms() - start;
        const double decode_start = process_cpu_ms();
        QImage decoded;
        for (int index = 0; index < 100; ++index)
        {
            decoded = QImage::fromData(bytes).convertToFormat(QImage::Format_RGB32);
        }
        if (decoded.isNull())
        {
            return 4;
        }
        const double decode_cpu = process_cpu_ms() - decode_start;
        qint64 error = 0;
        for (int y = 0; y < image.height(); ++y)
        {
            for (int x = 0; x < image.width(); ++x)
            {
                const QColor left = image.pixelColor(x, y);
                const QColor right = decoded.pixelColor(x, y);
                error += std::abs(left.red() - right.red()) + std::abs(left.green() - right.green()) +
                         std::abs(left.blue() - right.blue());
            }
        }
        rows.append(QJsonObject{{"format", static_cast<int>(option.format)},
            {"codec", QString::fromLatin1(option.codec)}, {"compression", option.compression},
            {"quality", option.quality}, {"cpu_ms_100_encodes", encode_cpu},
            {"cpu_ms_100_decodes", decode_cpu}, {"bytes", bytes.size()}, {"lossless", error == 0},
            {"mean_absolute_error", error / (3.0 * image.width() * image.height())}});
    }
    QFile file(output);
    if (!file.open(QIODevice::WriteOnly))
    {
        return 6;
    }
    file.write(QJsonDocument(rows).toJson());
    return 0;
}

static int header_benchmark(const QString& directory, const QString& output)
{
    QStringList paths;
    QDirIterator files(directory, {"*.jpg"}, QDir::Files, QDirIterator::Subdirectories);
    while (files.hasNext())
    {
        paths.append(files.next());
    }
    QJsonArray rows;
    for (int mode = 0; mode < 3; ++mode)
    {
        const qint64 read_start = proc_value("/proc/self/io", "rchar");
        const qint64 disk_start = proc_value("/proc/self/io", "read_bytes");
        const double cpu_start = process_cpu_ms();
        qint64 pixels = 0;
        for (const auto& path : std::as_const(paths))
        {
            QFile file(path);
            QIODevice::OpenMode flags = QIODevice::ReadOnly;
            if (mode != 0)
            {
                flags |= QIODevice::Unbuffered;
            }
            if (!file.open(flags))
            {
                return 2;
            }
            posix_fadvise(file.handle(), 0, 0, POSIX_FADV_DONTNEED);
            if (mode == 2)
            {
                posix_fadvise(file.handle(), 0, 0, POSIX_FADV_RANDOM);
            }
            QImageReader reader(&file);
            const QSize size = reader.size();
            if (!size.isValid())
            {
                return 3;
            }
            pixels += static_cast<qint64>(size.width()) * size.height();
            reader.transformation();
        }
        const QString mode_name = QStringList{"buffered", "unbuffered", "unbuffered_random"}.at(mode);
        rows.append(QJsonObject{{"mode", mode_name}, {"files", paths.size()}, {"pixels", pixels},
            {"cpu_ms", process_cpu_ms() - cpu_start},
            {"rchar", proc_value("/proc/self/io", "rchar") - read_start},
            {"read_bytes", proc_value("/proc/self/io", "read_bytes") - disk_start}});
    }
    QFile file(output);
    if (!file.open(QIODevice::WriteOnly))
    {
        return 6;
    }
    file.write(QJsonDocument(rows).toJson());
    return 0;
}

int main(int argc, char** argv)
{
    QApplication app(argc, argv);
    if (argc != 4)
    {
        return 1;
    }
    QLocale::setDefault(QLocale(QLocale::English, QLocale::UnitedStates));
    QImageReader::setAllocationLimit(kQtImageReaderAllocMB);
    if (QString::fromLocal8Bit(argv[1]) == "--headers")
    {
        return header_benchmark(QString::fromLocal8Bit(argv[2]), QString::fromLocal8Bit(argv[3]));
    }
    if (QString::fromLocal8Bit(argv[1]) == "--encode")
    {
        return encoding_benchmark(QString::fromLocal8Bit(argv[2]), QString::fromLocal8Bit(argv[3]));
    }
    if (QString::fromLocal8Bit(argv[1]) == "--viewer")
    {
        return viewer_benchmark(QString::fromLocal8Bit(argv[2]), QString::fromLocal8Bit(argv[3]));
    }
    main_window window;
    window.resize(1280, 800);
    window.show();
    QTest::qWait(200);
    performance_probe probe(window);
    if (QString::fromLocal8Bit(argv[1]) == "--ui")
    {
        return probe.run(QString::fromLocal8Bit(argv[2]), QString::fromLocal8Bit(argv[3]), performance_probe::run_mode::capture);
    }
    auto mode = performance_probe::run_mode::cold;
    if (QString::fromLocal8Bit(argv[3]) == "warm")
    {
        mode = performance_probe::run_mode::warm;
    }
    else if (QString::fromLocal8Bit(argv[3]) == "trim")
    {
        mode = performance_probe::run_mode::trim;
    }
    else if (QString::fromLocal8Bit(argv[3]) == "soak")
    {
        mode = performance_probe::run_mode::soak;
    }
    return probe.run(QString::fromLocal8Bit(argv[1]), QString::fromLocal8Bit(argv[2]), mode);
}
