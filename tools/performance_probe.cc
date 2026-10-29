#include <QApplication>
#include <QDateTime>
#include <QEventLoop>
#include <QCollator>
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
#include <QMessageBox>
#include <QAbstractButton>
#include <QToolButton>
#include <QRandomGenerator>
#include <QTemporaryDir>
#include <QAction>
#include <QScrollBar>
#include <QStandardPaths>
#include <QtTest>
#include <atomic>
#include <array>
#include <chrono>
#include <QScreen>
#include <QWindow>
#include <QTimer>
#include <QWheelEvent>
#include <QMutex>
#include <QMutexLocker>
#include <unistd.h>
#include <sys/syscall.h>
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

static qint64 clock_ns()
{
    return std::chrono::duration_cast<std::chrono::nanoseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
}

struct latency_stats
{
    static constexpr int kLastBucketMs = 1000;
    std::array<quint64, kLastBucketMs + 1> bins{};
    quint64 count = 0;
    qint64 total_ns = 0;
    qint64 max_ns = 0;
    quint64 overflow = 0;

    void add(qint64 ns)
    {
        ns = std::max<qint64>(0, ns);
        ++count;
        total_ns += ns;
        max_ns = std::max(max_ns, ns);
        const qint64 bucket = (ns + 999999) / 1000000;
        ++bins[std::min<qint64>(bucket, kLastBucketMs)];
        overflow += bucket > kLastBucketMs;
    }

    QJsonObject json() const
    {
        const auto percentile = [this](int percent)
        {
            quint64 cumulative = 0;
            for (int index = 0; index <= kLastBucketMs; ++index)
            {
                cumulative += bins[index];
                if (cumulative * 100 >= count * percent)
                {
                    return index;
                }
            }
            return kLastBucketMs;
        };
        return {{"count", static_cast<qint64>(count)},
                {"mean_ms", count == 0 ? 0.0 : total_ns / (1000000.0 * count)},
                {"max_ms", max_ns / 1000000.0}, {"p50_ms_upper", percentile(50)},
                {"p95_ms_upper", percentile(95)}, {"p99_ms_upper", percentile(99)},
                {"over_1000ms", static_cast<qint64>(overflow)}};
    }
};

class performance_probe : public QObject
{
   public:
    enum class run_mode { cold, warm, capture, trim, soak, usage, paced, profile };
    explicit performance_probe(main_window& window) : QObject(&window), window_(window)
    {
        timer_.start();
        window_.view_->viewport()->installEventFilter(this);
        // 先记录提交，再调用原有槽；避免很快的缓存命中先于探针记时。
        QObject::disconnect(window_.scene_, &waterfall_scene::request_load_batch,
                            window_.image_loader_, &image_loader::request_thumbnails);
        QObject::connect(window_.scene_, &waterfall_scene::request_load_batch, this,
                         [this](const QList<load_task>& tasks)
        {
            QMutexLocker lock(&timing_mutex_);
            for (const auto& task : tasks)
            {
                pending_.insert({task.session_id, task.id}, {task.path, clock_ns(), 0});
            }
        });
        QObject::connect(window_.scene_, &waterfall_scene::request_load_batch,
                         window_.image_loader_, &image_loader::request_thumbnails, Qt::DirectConnection);
        QObject::connect(window_.image_loader_, &image_loader::thumbnail_started, this,
                         [this](quint64 id, int session)
        {
            const qint64 now = clock_ns();
            QMutexLocker lock(&timing_mutex_);
            auto it = pending_.find({session, id});
            if (it != pending_.end())
            {
                it->started = now;
                queue_latency_.add(now - it->submitted);
            }
        }, Qt::DirectConnection);
        QObject::connect(window_.scene_, &waterfall_scene::request_cancel_batch, this,
                         [this](const QList<QString>& paths)
        {
            QMutexLocker lock(&timing_mutex_);
            pending_.removeIf([&paths](const QHash<QPair<int, quint64>, task_timing>::iterator& entry)
            {
                return entry->started == 0 && paths.contains(entry->path);
            });
            display_pending_.removeIf([&paths](const QHash<QPair<int, quint64>, paint_timing>::iterator& entry) { return paths.contains(entry->path); });
        });
        QObject::connect(window_.scene_, &waterfall_scene::request_cancel_all, this, [this]()
        {
            QMutexLocker lock(&timing_mutex_);
            pending_.removeIf([](const QHash<QPair<int, quint64>, task_timing>::iterator& entry) { return entry->started == 0; });
            display_pending_.clear();
        });
        QMetaObject::invokeMethod(window_.image_loader_, [this]() { loader_tid_ = syscall(SYS_gettid); },
                                  Qt::BlockingQueuedConnection);
        heartbeat_.setInterval(16);
        QObject::connect(&heartbeat_, &QTimer::timeout, this, [this]()
        {
            const qint64 now = clock_ns();
            if (heartbeat_ns_ != 0)
            {
                heartbeat_delay_.add(now - heartbeat_ns_ - 16000000);
            }
            heartbeat_ns_ = now;
        });
        QObject::connect(window_.scene_, &waterfall_scene::request_load_batch, &window_, [this]()
        {
            QMutexLocker locker(&window_.image_loader_->mutex_);
            max_queue_ = std::max(max_queue_, window_.image_loader_->task_queue_.size());
            max_active_ = std::max(max_active_, window_.scene_->active_items_.size());
        });
        QObject::connect(window_.file_scanner_, &file_scanner::images_scanned_batch, &window_, [this](const QList<image_meta>&, int session)
        {
            if (session == window_.current_scan_session_id_ && first_batch_ms_ < 0)
            {
                first_batch_ms_ = timer_.elapsed();
            }
        });
        QObject::connect(window_.file_scanner_, &file_scanner::scan_finished, &window_, [this](int, qint64 ms, int session)
        {
            if (session != window_.current_scan_session_id_)
            {
                return;
            }
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
                         [this](quint64 id, const QString& path, const QImage& image, int session,
                                image_loader::thumbnail_source source)
        {
            const qint64 now = clock_ns();
            ++source_counts_[static_cast<int>(source)];
            const QRectF visible = window_.view_->mapToScene(window_.view_->viewport()->rect()).boundingRect();
            const bool paintable = std::any_of(window_.scene_->active_items_.cbegin(), window_.scene_->active_items_.cend(),
                [id, &visible](const waterfall_item* item)
                {
                    return item->get_request_id() == id && item->sceneBoundingRect().intersects(visible);
                });
            {
                QMutexLocker lock(&timing_mutex_);
                auto it = pending_.find({session, id});
                if (it != pending_.end())
                {
                    if (it->started != 0)
                    {
                        ready_latency_.add(now - it->started);
                    }
                    callback_latency_.add(now - it->submitted);
                    if (!image.isNull() && paintable && session == window_.scene_->current_session_id_)
                    {
                        display_pending_.insert({session, id}, {path, it->submitted, now});
                    }
                    pending_.erase(it);
                }
            }
            ++decoded_count_;
            if (!image.isNull() && first_thumbnail_ms_ < 0)
            {
                first_thumbnail_ms_ = timer_.elapsed();
            }
            if (!image.isNull())
            {
                bool matched = false;
                for (auto* item : window_.scene_->active_items_)
                {
                    if (item->get_request_id() != id)
                    {
                        continue;
                    }
                    matched = true;
                    if (item->sceneBoundingRect().intersects(visible))
                    {
                        ++visible_results_;
                        if (first_visible_ms_ < 0)
                        {
                            first_visible_ms_ = timer_.elapsed();
                        }
                    }
                    else
                    {
                        ++prefetch_results_;
                    }
                    break;
                }
                if (!matched)
                {
                    ++stale_results_;
                }
            }
            else
            {
                ++failed_results_;
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

    void sample(const QString& phase, QFile* stream = nullptr)
    {
        auto* scene = window_.scene_;
        auto* loader = window_.image_loader_;
        rusage usage{};
        getrusage(RUSAGE_SELF, &usage);
        QJsonObject row{{"minor_faults", static_cast<qint64>(usage.ru_minflt)},
                        {"major_faults", static_cast<qint64>(usage.ru_majflt)},
                        {"rusage_peak_rss_kib", static_cast<qint64>(usage.ru_maxrss)},
                        {"phase", phase}, {"elapsed_ms", timer_.elapsed()}, {"cpu_ms", process_cpu_ms()},
                        {"timestamp_utc", QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs)},
                        {"threads", proc_value("/proc/self/status", "Threads")},
                        {"file_descriptors", QDir("/proc/self/fd").entryList(QDir::AllEntries | QDir::NoDotAndDotDot).size()},
                        {"rss_kib", proc_value("/proc/self/status", "VmRSS")},
                        {"peak_rss_kib", proc_value("/proc/self/status", "VmHWM")},
                        {"pss_kib", proc_value("/proc/self/smaps_rollup", "Pss")},
                        {"model_count", static_cast<qint64>(scene->all_models_.size())},
                        {"metadata_capacity_bytes", static_cast<qint64>(scene->all_models_.capacity() * sizeof(layout_model) +
                            scene->item_bottom_index_.capacity() * sizeof(int) + scene->image_paths_.capacity() * sizeof(QString))},
                        {"active_items", scene->active_items_.size()}, {"pool_items", scene->pool_.size()},
                        {"total_items", scene->items().size()},
                        {"decoded_count", decoded_count_}, {"visible_results", visible_results_},
                        {"prefetch_results", prefetch_results_}, {"stale_results", stale_results_},
                        {"failed_results", failed_results_},
                        {"scan_cpu_ms", scan_cpu_ms_.load()},
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
        for (const QByteArray& field : {"rchar", "wchar", "syscr", "syscw", "read_bytes", "write_bytes", "cancelled_write_bytes"})
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
        row["source_counts"] = QJsonObject{{"memory", source_counts_[0]}, {"disk", source_counts_[1]},
            {"decoded", source_counts_[2]}, {"failed", source_counts_[3]}};
        row["latency"] = latency_json();
        row["viewer_visible"] = window_.viewer_window_ != nullptr && window_.viewer_window_->isVisible();
        if (stream != nullptr)
        {
            stream->write(QJsonDocument(row).toJson(QJsonDocument::Compact) + '\n');
            stream->flush();
            return;
        }
        samples_.append(row);
    }

    bool capture_ui(QWidget& widget, const QString& output, const QString& phase)
    {
        auto* header = widget.findChild<QWidget*>("window_title_bar");
        for (auto* button : header->findChildren<QToolButton*>())
        {
            if (button->objectName() == "qt_toolbar_ext_button") { continue; }
            if (!button->isVisibleTo(&widget) || !widget.rect().contains(QRect(button->mapTo(&widget, QPoint()), button->size())))
            {
                qWarning() << "标题栏按钮被裁切" << phase << button->text();
                return false;
            }
        }
        if (!widget.screen()->grabWindow(widget.winId()).save(output + "/" + phase + ".png")) { return false; }
        const double cpu = process_cpu_ms();
        QTest::qWait(500);
        const double idle = process_cpu_ms() - cpu;
        sample(phase);
        QImage surface(widget.size() * widget.devicePixelRatioF(), QImage::Format_ARGB32_Premultiplied);
        surface.setDevicePixelRatio(widget.devicePixelRatioF());
        std::vector<double> frames;
        for (int frame = 0; frame < 60; ++frame)
        {
            QElapsedTimer elapsed;
            elapsed.start();
            QPainter painter(&surface);
            widget.render(&painter);
            painter.end();
            frames.push_back(elapsed.nsecsElapsed() / 1000000.0);
        }
        std::sort(frames.begin(), frames.end());
        QJsonObject row = samples_.last().toObject();
        row["whole_window_render_frames"] = 60;
        row["whole_window_render_median_ms"] = frames[30];
        row["whole_window_render_p95_ms"] = frames[57];
        row["loaded_idle_cpu_ms_500ms"] = idle;
        row["render_surface_bytes"] = surface.sizeInBytes();
        row["peak_rss_after_render_kib"] = proc_value("/proc/self/status", "VmHWM");
        row["widget_count"] = widget.findChildren<QWidget*>().size();
        row["window_size"] = QJsonArray{widget.width(), widget.height()};
        samples_[samples_.size() - 1] = row;
        QFile file(output + "/review-metrics.json");
        if (!file.open(QIODevice::WriteOnly)) { return false; }
        const QByteArray data = QJsonDocument(samples_).toJson();
        return file.write(data) == data.size();
    }

    int run(const QString& directory, const QString& output, run_mode mode)
    {
        const bool clear_disk = mode == run_mode::warm || mode == run_mode::soak;
        sample("empty");
        const double idle_start = process_cpu_ms();
        QTest::qWait(500);
        idle_cpu_ms_ = process_cpu_ms() - idle_start;
        timer_.restart();
        heartbeat_.start();
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
            window_.raise();
            window_.activateWindow();
            QTest::qWaitForWindowExposed(&window_);
            QTest::qWait(300);
            if (!capture_ui(window_, output, "light")) { return 6; }
            window_.dark_theme_action_->setChecked(true);
            QTest::qWait(300);
            if (!capture_ui(window_, output, "dark")) { return 6; }
            const QSize wide = window_.size();
            window_.resize(640, 480);
            QTest::qWait(180);
            if (!wait_idle() || window_.width() != 640) { return 4; }
            if (!capture_ui(window_, output, "narrow")) { return 6; }
            window_.resize(wide);
            QTest::qWait(180);
            if (!wait_idle()) { return 4; }
            window_.show_image_viewer(window_.scene_->image_paths_.first(), window_.scene_->get_all_paths());
            QElapsedTimer wait;
            wait.start();
            while (!window_.viewer_window_->windowTitle().startsWith("Viewer -") && wait.elapsed() < 10000)
            {
                QTest::qWait(20);
            }
            window_.viewer_window_->raise();
            window_.viewer_window_->activateWindow();
            QTest::qWait(300);
            if (!capture_ui(*window_.viewer_window_, output, "preview")) { return 6; }
            window_.viewer_window_->resize(640, 480);
            QTest::qWait(180);
            if (window_.viewer_window_->width() != 640) { return 4; }
            if (!capture_ui(*window_.viewer_window_, output, "preview_narrow")) { return 6; }
            window_.viewer_window_->close();
            window_.dark_theme_action_->setChecked(false);
            QTemporaryDir empty;
            window_.open_path(empty.path(), false);
            window_.raise();
            window_.activateWindow();
            QTest::qWait(300);
            if (!capture_ui(window_, output, "empty")) { return 6; }
            return 0;
        }
        QImage surface(window_.view_->size(), QImage::Format_RGB32);
        double render_ms = 0;
        auto* scroll = window_.view_->verticalScrollBar();
        const bool natural = mode == run_mode::usage || mode == run_mode::paced || mode == run_mode::profile;
        const int laps = mode == run_mode::soak ? 30 : (mode == run_mode::profile ? 1 : 3);
        for (int lap = 0; lap < laps; ++lap)
        {
            const int steps = mode == run_mode::profile ? 12 : 120;
            for (int step = 0; step < steps; ++step)
            {
                if (natural && lap == 0)
                {
                    // 连续滚动走视图原有 16ms 合并路径，不强制刷新可见区。
                    scroll->setValue(scroll->value() + window_.view_->viewport()->height() / 5);
                }
                else
                {
                    const int position = natural ? (step * 73 + lap * 19) % 120 : (lap % 2 == 0 ? step : 119 - step);
                    scroll->setValue(static_cast<qint64>(scroll->maximum()) * position / 119);
                }
                if (!natural)
                {
                    window_.view_->check_visible_area();
                }
                QTest::qWait(40);
                if (mode == run_mode::paced && !wait_idle())
                {
                    return 9;
                }
                if (natural)
                {
                    continue;
                }
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
        if (mode == run_mode::usage || mode == run_mode::paced)
        {
            window_.showMaximized();
            QTest::qWait(300);
            wait_idle();
            window_.showNormal();
            QTest::qWait(300);
            wait_idle();
            sample("restored");
            QTemporaryDir empty;
            scan_done_ = false;
            window_.open_path(empty.path(), false);
            if (!wait_idle()) { return 7; }
            sample("switched_empty");
            scan_done_ = false;
            window_.open_path(directory, false);
            if (!wait_idle()) { return 8; }
            sample("reopened");
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
        heartbeat_.stop();
        QJsonArray screen_info;
        for (const auto* screen : QGuiApplication::screens())
        {
            screen_info.append(QJsonObject{{"name", screen->name()}, {"dpr", screen->devicePixelRatio()},
                {"geometry", QJsonArray{screen->geometry().width(), screen->geometry().height()}},
                {"logical_dpi", screen->logicalDotsPerInch()}});
        }
        QJsonObject result{{"platform", QGuiApplication::platformName()},
                           {"window_logical_px", QJsonArray{window_.width(), window_.height()}},
                           {"viewport_logical_px", QJsonArray{window_.view_->viewport()->width(), window_.view_->viewport()->height()}},
                           {"dpr", window_.view_->devicePixelRatioF()}, {"screens", screen_info},
                           {"loader_tid", loader_tid_}, {"pid", static_cast<qint64>(getpid())},
                           {"latency", latency_json()}, {"qt", qVersion()}, {"dataset", directory},
                           {"first_batch_ms", first_batch_ms_}, {"first_thumbnail_ms", first_thumbnail_ms_},
                           {"first_visible_ms", first_visible_ms_},
                           {"scan_ms", scan_ms_}, {"idle_cpu_ms_500ms", idle_cpu_ms_},
                           {"max_queue_sampled", max_queue_}, {"max_active_items", max_active_},
                           {"clear_disk_at_end", clear_disk},
                           {QString("explicit_render_ms_%1_frames").arg(natural ? 0 : laps * 120), render_ms}, {"samples", samples_}};
        QFile file(output);
        if (!file.open(QIODevice::WriteOnly))
        {
            return 6;
        }
        file.write(QJsonDocument(result).toJson());
        return 0;
    }

    int run_photos(const QString& directory, const QString& output)
    {
        window_.open_path(directory, false);
        if (!wait_idle()) { return 2; }
        QJsonObject checks;
        checks["model_count"] = static_cast<qint64>(window_.scene_->all_models_.size());
        QCollator collator(QLocale(QLocale::English));
        collator.setNumericMode(true);
        collator.setCaseSensitivity(Qt::CaseInsensitive);
        for (int mode = 0; mode < 3; ++mode)
        {
            for (bool descending : {false, true})
            {
                if (static_cast<int>(window_.current_sort_mode_) != mode || window_.sort_descending_ != descending)
                {
                    scan_done_ = false;
                    window_.scene_->request_sort(mode, descending);
                    if (!wait_idle()) { return 3; }
                }
                const auto paths = window_.scene_->get_all_paths();
                for (qsizetype index = 1; index < paths.size(); ++index)
                {
                    const QFileInfo a(paths.at(index - 1)), b(paths.at(index));
                    int comparison = collator.compare(a.fileName(), b.fileName());
                    if (comparison == 0) { comparison = QString::compare(a.filePath(), b.filePath(), Qt::CaseInsensitive); }
                    if (mode == 1 && a.lastModified() != b.lastModified())
                    {
                        comparison = a.lastModified() < b.lastModified() ? -1 : 1;
                    }
                    if (mode == 2 && a.size() != b.size()) { comparison = a.size() < b.size() ? -1 : 1; }
                    if (descending ? comparison < 0 : comparison > 0) { return 4; }
                }
            }
        }
        checks["sort_cases_passed"] = 6;
        // 只抽取极值和旋转图；不复制整个图库路径，也不输出私人文件名。
        QStringList previews;
        qint64 largest_pixels = 0;
        QString largest;
        int rotated = 0;
        for (size_t index = 0; index < window_.scene_->all_models_.size(); ++index)
        {
            const auto size = window_.scene_->all_models_[index].original_size;
            const qint64 pixels = static_cast<qint64>(size.width()) * size.height();
            const QString path = window_.scene_->image_paths_.at(index);
            if (pixels > largest_pixels) { largest_pixels = pixels; largest = path; }
            QImageReader reader(path);
            if (reader.transformation().testFlag(QImageIOHandler::TransformationRotate90))
            {
                previews.append(path);
                ++rotated;
            }
        }
        previews.append(largest);
        for (int step = 0; step < 12 && !window_.scene_->image_paths_.isEmpty(); ++step)
        {
            previews.append(window_.scene_->image_paths_.at(static_cast<qint64>(window_.scene_->image_paths_.size() - 1) * step / 11));
        }
        previews.removeDuplicates();
        int preview_count = 0;
        int preview_failures = 0;
        for (const auto& path : previews)
        {
            window_.show_image_viewer(path, previews);
            QElapsedTimer decode;
            decode.start();
            while (!window_.viewer_window_->windowTitle().startsWith("Viewer -") &&
                   window_.viewer_window_->windowTitle() != "加载图片失败" && decode.elapsed() < 30000)
            {
                QTest::qWait(20);
            }
            if (!window_.viewer_window_->windowTitle().startsWith("Viewer -")) { ++preview_failures; }
            else
            {
                auto* view = window_.viewer_window_->findChild<QGraphicsView*>();
                auto* item = qgraphicsitem_cast<QGraphicsPixmapItem*>(view->scene()->items().first());
                const qsizetype index = window_.scene_->image_paths_.indexOf(path);
                const QSize expected = window_.scene_->all_models_[index].original_size;
                constexpr qreal kAspectTolerance = 0.01;
                if (item != nullptr && std::abs((item->pixmap().width() * static_cast<qreal>(expected.height())) /
                                               (item->pixmap().height() * static_cast<qreal>(expected.width())) - 1) < kAspectTolerance)
                {
                    ++preview_count;
                }
                else { ++preview_failures; }
            }
            QMetaObject::invokeMethod(window_.viewer_window_, "rotate_right");
            QMetaObject::invokeMethod(window_.viewer_window_, "set_fit_width_mode");
            QTest::qWait(80);
            sample("photo_preview");
            window_.viewer_window_->close();
            QTest::qWait(40);
        }
        checks["rotated_metadata_count"] = rotated;
        checks["largest_pixels"] = largest_pixels;
        checks["preview_passed"] = preview_count;
        checks["preview_failed"] = preview_failures;
        sample("photos_closed");
        // 删除路径只指向独立临时副本，原照片目录不进入删除调用。
        QTemporaryDir scratch;
        const QString copy = scratch.filePath("imageviewer-delete-copy.jpg");
        if (largest.isEmpty() || !QFile::copy(largest, copy)) { return 5; }
        window_.open_path(scratch.path(), false);
        scan_done_ = false;
        if (!wait_idle()) { return 6; }
        window_.show_image_viewer(copy, {copy});
        QTimer confirm;
        confirm.setInterval(20);
        bool trash_failed = false;
        QObject::connect(&confirm, &QTimer::timeout, &window_, [&copy, &trash_failed]()
        {
            auto* box = qobject_cast<QMessageBox*>(QApplication::activeModalWidget());
            if (box != nullptr && box->windowTitle() == "移到回收站" && box->text().contains(QFileInfo(copy).fileName()))
            {
                box->button(QMessageBox::Yes)->click();
            }
            else if (box != nullptr && box->windowTitle() == "移到回收站失败")
            {
                trash_failed = true;
                box->button(QMessageBox::Ok)->click();
            }
        });
        confirm.start();
        window_.on_move_path_to_trash(copy);
        confirm.stop();
        if (trash_failed || QFileInfo::exists(copy) || !QFileInfo::exists(largest) || !wait_idle()) { return 7; }
        checks["temporary_copy_deleted"] = true;
        checks["model_count_after_delete"] = static_cast<qint64>(window_.scene_->all_models_.size());
        if (!window_.scene_->all_models_.empty()) { return 8; }
        window_.viewer_window_->close();
        QMetaObject::invokeMethod(window_.image_loader_, &image_loader::clear_cache, Qt::BlockingQueuedConnection);
        QTest::qWait(50);
        sample("deleted_and_cleared");
        checks["samples"] = samples_;
        checks["platform"] = QGuiApplication::platformName();
        checks["dpr"] = window_.devicePixelRatioF();
        QFile result(output);
        if (!result.open(QIODevice::WriteOnly)) { return 9; }
        result.write(QJsonDocument(checks).toJson());
        return preview_failures == 0 ? 0 : 10;
    }

    int run_long(const QString& directory, const QString& output)
    {
        const int seconds = qEnvironmentVariableIntValue("IV_BENCH_SECONDS");
        const QString alternate = qEnvironmentVariable("IV_BENCH_ALTERNATE");
        const QString fixtures = qEnvironmentVariable("IV_BENCH_FIXTURES");
        if (seconds < 1 || alternate.isEmpty() || fixtures.isEmpty()) { return 1; }
        QFile stream(output);
        if (!stream.open(QIODevice::WriteOnly | QIODevice::NewOnly)) { return 2; }
        timer_.restart();
        stream.write(QJsonDocument(QJsonObject{{"event", "start"}, {"seconds_requested", seconds},
            {"started_utc", QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs)},
            {"source", directory}, {"alternate", alternate}, {"fixtures", fixtures},
            {"pid", static_cast<qint64>(getpid())}, {"qt", qVersion()},
            {"platform", QGuiApplication::platformName()}, {"dpr", window_.devicePixelRatioF()}}).toJson(QJsonDocument::Compact) + '\n');
        stream.flush();
        QString phase = "opening";
        QTimer sampler;
        sampler.setInterval(30000);
        QObject::connect(&sampler, &QTimer::timeout, this, [&]() { sample(phase, &stream); });
        sampler.start();
        window_.open_path(directory, false);
        if (!wait_idle()) { return 3; }
        QStringList previews;
        qint64 largest_pixels = 0;
        QString largest;
        for (size_t index = 0; index < window_.scene_->all_models_.size(); ++index)
        {
            const QSize size = window_.scene_->all_models_[index].original_size;
            const qint64 pixels = static_cast<qint64>(size.width()) * size.height();
            if (pixels > largest_pixels)
            {
                largest_pixels = pixels;
                largest = window_.scene_->image_paths_.at(index);
            }
        }
        if (!largest.isEmpty()) { previews.append(largest); }
        if (!window_.scene_->image_paths_.isEmpty())
        {
            previews.append(window_.scene_->image_paths_.at(window_.scene_->image_paths_.size() / 2));
        }
        QDirIterator extra(fixtures, {"*.gif", "*.webp", "*.png"}, QDir::Files, QDirIterator::Subdirectories);
        while (extra.hasNext())
        {
            const QString path = extra.next();
            if (QImageReader(path).canRead()) { previews.append(path); }
        }
        QRandomGenerator random(0x496d6167);
        int direction = 1;
        int cycles = 0;
        qint64 scroll_steps = 0, jumps = 0, preview_count = 0, switches = 0;
        auto* scroll = window_.view_->verticalScrollBar();
        const qint64 duration_ms = static_cast<qint64>(seconds) * 1000;
        while (timer_.elapsed() < duration_ms)
        {
            phase = "continuous";
            for (int step = 0; step < 120 && timer_.elapsed() < duration_ms; ++step)
            {
                if (scroll->value() == scroll->maximum()) { direction = -1; }
                if (scroll->value() == scroll->minimum()) { direction = 1; }
                scroll->setValue(scroll->value() + direction * window_.view_->viewport()->height() / 3);
                ++scroll_steps;
                QTest::qWait(150);
            }
            if (timer_.elapsed() >= duration_ms) { break; }
            phase = "random";
            for (int step = 0; step < 16 && timer_.elapsed() < duration_ms; ++step)
            {
                scroll->setValue(random.bounded(scroll->maximum() + 1));
                ++jumps;
                QTest::qWait(350);
            }
            if (timer_.elapsed() >= duration_ms) { break; }
            phase = "resizing";
            window_.resize(cycles % 2 == 0 ? 1000 : 1400, 800);
            QTest::qWait(350);
            if (!wait_idle()) { return 4; }
            if (timer_.elapsed() >= duration_ms) { break; }
            phase = "preview";
            for (int step = 0; step < 3 && !previews.isEmpty() && timer_.elapsed() < duration_ms; ++step)
            {
                const QString path = previews.at((cycles * 3 + step) % previews.size());
                window_.show_image_viewer(path, previews);
                QElapsedTimer decode;
                decode.start();
                while (!window_.viewer_window_->windowTitle().startsWith("Viewer -") &&
                       window_.viewer_window_->windowTitle() != "加载图片失败" && decode.elapsed() < 30000)
                {
                    QTest::qWait(20);
                }
                if (!window_.viewer_window_->windowTitle().startsWith("Viewer -")) { return 5; }
                ++preview_count;
                QMetaObject::invokeMethod(window_.viewer_window_, "zoom_in");
                QMetaObject::invokeMethod(window_.viewer_window_, "rotate_right");
                QTest::qWait(400);
            }
            sample("preview_open", &stream);
            if (window_.viewer_window_ != nullptr) { window_.viewer_window_->close(); }
            QTest::qWait(60);
            sample("preview_closed", &stream);
            if (timer_.elapsed() >= duration_ms) { break; }
            phase = "switching";
            scan_done_ = false;
            window_.open_path(cycles % 2 == 0 ? alternate : directory, false);
            ++switches;
            if (!wait_idle()) { return 6; }
            sample("directory_ready", &stream);
            phase = "idle";
            QElapsedTimer idle;
            idle.start();
            const double idle_cpu = process_cpu_ms();
            QEventLoop idle_loop;
            QTimer::singleShot(std::clamp<qint64>(duration_ms - timer_.elapsed(), 0, 10000), &idle_loop, &QEventLoop::quit);
            idle_loop.exec();
            const double idle_cpu_ms = process_cpu_ms() - idle_cpu;
            const qint64 idle_duration_ms = idle.elapsed();
            sample("idle_end", &stream);
            stream.write(QJsonDocument(QJsonObject{{"event", "idle"}, {"elapsed_ms", timer_.elapsed()},
                {"duration_ms", idle_duration_ms}, {"cpu_ms", idle_cpu_ms}}).toJson(QJsonDocument::Compact) + '\n');
            stream.flush();
            ++cycles;
            if (cycles % 10 == 0)
            {
                phase = "clearing";
                window_.scene_->clear_items();
                QMetaObject::invokeMethod(window_.image_loader_, &image_loader::clear_cache, Qt::BlockingQueuedConnection);
                sample("periodic_cleared", &stream);
                scan_done_ = false;
                window_.open_path(directory, false);
                if (!wait_idle()) { return 7; }
            }
        }
        sampler.stop();
        if (window_.viewer_window_ != nullptr) { window_.viewer_window_->close(); }
        window_.scene_->clear_items();
        QMetaObject::invokeMethod(window_.image_loader_, &image_loader::clear_cache, Qt::BlockingQueuedConnection);
        QTest::qWait(60);
        sample("final_cleared", &stream);
        stream.write(QJsonDocument(QJsonObject{{"event", "finished"}, {"elapsed_ms", timer_.elapsed()},
            {"finished_utc", QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs)}, {"cycles", cycles},
            {"scroll_steps", scroll_steps}, {"random_jumps", jumps}, {"previews", preview_count},
            {"directory_switches", switches}, {"max_active_items", max_active_}, {"max_queue_sampled", max_queue_}}).toJson(QJsonDocument::Compact) + '\n');
        stream.flush();
        return 0;
    }

   protected:
    bool eventFilter(QObject* watched, QEvent* event) override
    {
        if (event->type() == QEvent::Paint)
        {
            const qint64 now = clock_ns();
            const QRectF visible = window_.view_->mapToScene(window_.view_->viewport()->rect()).boundingRect();
            for (auto* item : window_.scene_->active_items_)
            {
                const QPair<int, quint64> key{window_.scene_->current_session_id_, item->get_request_id()};
                auto it = display_pending_.find(key);
                if (it == display_pending_.end() || item->state() != waterfall_item::load_state::ready ||
                    !item->sceneBoundingRect().intersects(visible))
                {
                    continue;
                }
                paint_latency_.add(now - it->ready);
                visible_latency_.add(now - it->submitted);
                display_pending_.erase(it);
            }
        }
        return QObject::eventFilter(watched, event);
    }

   private:
    QJsonObject latency_json()
    {
        QMutexLocker lock(&timing_mutex_);
        return {{"queue", queue_latency_.json()}, {"worker_to_callback", ready_latency_.json()},
                {"submit_to_callback", callback_latency_.json()}, {"callback_to_paint", paint_latency_.json()},
                {"submit_to_paint", visible_latency_.json()}, {"gui_timer_delay", heartbeat_delay_.json()},
                {"pending_tasks", pending_.size()}, {"pending_paint", display_pending_.size()}};
    }

    struct task_timing { QString path; qint64 submitted; qint64 started; };
    struct paint_timing { QString path; qint64 submitted; qint64 ready; };
    QHash<QPair<int, quint64>, task_timing> pending_;
    QHash<QPair<int, quint64>, paint_timing> display_pending_;
    QMutex timing_mutex_;
    latency_stats queue_latency_, ready_latency_, callback_latency_, paint_latency_, visible_latency_, heartbeat_delay_;
    std::array<int, 4> source_counts_{};
    QTimer heartbeat_;
    qint64 heartbeat_ns_ = 0;
    int loader_tid_ = 0;
    main_window& window_;
    QElapsedTimer timer_;
    bool scan_done_ = false;
    qint64 first_batch_ms_ = -1;
    qint64 first_thumbnail_ms_ = -1;
    qint64 first_visible_ms_ = -1;
    qint64 scan_ms_ = 0;
    int decoded_count_ = 0;
    int visible_results_ = 0;
    int prefetch_results_ = 0;
    int stale_results_ = 0;
    int failed_results_ = 0;
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
    const QStringList dimensions = qEnvironmentVariable("IV_BENCH_WINDOW", "1280x800").split('x');
    window.resize(dimensions.at(0).toInt(), dimensions.at(1).toInt());
    window.show();
    QTest::qWait(200);
    auto* probe = new performance_probe(window);
    if (QString::fromLocal8Bit(argv[1]) == "--ui")
    {
        return probe->run(QString::fromLocal8Bit(argv[2]), QString::fromLocal8Bit(argv[3]), performance_probe::run_mode::capture);
    }
    if (QString::fromLocal8Bit(argv[3]) == "photos")
    {
        return probe->run_photos(QString::fromLocal8Bit(argv[1]), QString::fromLocal8Bit(argv[2]));
    }
    if (QString::fromLocal8Bit(argv[3]) == "long")
    {
        return probe->run_long(QString::fromLocal8Bit(argv[1]), QString::fromLocal8Bit(argv[2]));
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
    else if (QString::fromLocal8Bit(argv[3]) == "usage")
    {
        mode = performance_probe::run_mode::usage;
    }
    else if (QString::fromLocal8Bit(argv[3]) == "paced")
    {
        mode = performance_probe::run_mode::paced;
    }
    else if (QString::fromLocal8Bit(argv[3]) == "profile")
    {
        mode = performance_probe::run_mode::profile;
    }
    else if (QString::fromLocal8Bit(argv[3]) == "soak")
    {
        mode = performance_probe::run_mode::soak;
    }
    return probe->run(QString::fromLocal8Bit(argv[1]), QString::fromLocal8Bit(argv[2]), mode);
}
