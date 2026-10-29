#include <QApplication>
#include <QBuffer>
#include <QGraphicsView>
#include <QGraphicsPixmapItem>
#include <QMovie>
#include <QPointer>
#include <QImageReader>
#include <QFile>
#include <QFileInfo>
#include <QDateTime>
#include <QDropEvent>
#include <QMimeData>
#include <QDir>
#include <QLocale>
#include <QStandardPaths>
#include <QSemaphore>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QThread>
#include <QtTest>
#include <QtConcurrent>
#include "common_types.h"
#include "file_scanner.h"
#include "image_loader.h"
#include "image_viewer_window.h"
#include "main_window.h"
#include "waterfall_item.h"
#include "waterfall_scene.h"
#include "waterfall_view.h"
#include <QAction>
#include <QScrollBar>
#include <QScopeGuard>
#include <QWheelEvent>
#include <QScreen>
#include <QMessageBox>
#include <QAbstractButton>
#include <QLineEdit>
#include <QToolButton>
#include <QMenu>
#include <QToolTip>
#include <QToolBar>
#include <QStatusBar>
#include <QSettings>
#include <QLibrary>
#include <QCursor>
#include <memory>

namespace
{
constexpr char kAnimatedGif[] =
    "R0lGODlhAgACAIEAAP8AAAAAAAAAAAAAACH/C05FVFNDQVBFMi4wAwEAAAAh+QQACgAAACwAAAAAAgACAAAIBgABCAQQEAAh+QQBCgABACwAAAAAAgACAIEAAP8AAAAAAAAAAAAIBgABCAQQEAA7";

bool drop_folder(main_window& window, const QString& path)
{
    QMimeData mime;
    mime.setUrls({QUrl::fromLocalFile(path)});
    QDragEnterEvent enter(QPoint(1, 1), Qt::CopyAction, &mime, Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(&window, &enter);
    QDropEvent drop(QPointF(1, 1), Qt::CopyAction, &mime, Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(&window, &drop);
    return enter.isAccepted() && drop.isAccepted();
}
}

class scaled_view : public waterfall_view
{
   public:
    qreal ratio = 1;

   protected:
    int metric(PaintDeviceMetric metric) const override
    {
        if (metric == PdmDevicePixelRatio) { return qRound(ratio); }
        if (metric == PdmDevicePixelRatioScaled) { return qRound(ratio * devicePixelRatioFScale()); }
#if QT_VERSION >= QT_VERSION_CHECK(6, 8, 0)
        if (metric == PdmDevicePixelRatioF_EncodedA || metric == PdmDevicePixelRatioF_EncodedB)
        {
            return encodeMetricF(metric, ratio);
        }
#endif
        return waterfall_view::metric(metric);
    }
};

class loader_worker
{
   public:
    loader_worker()
    {
        loader = new image_loader();
        loader->moveToThread(&thread);
        QObject::connect(&thread, &QThread::finished, loader, &QObject::deleteLater);
    }

    ~loader_worker()
    {
        loader->stop();
        if (!thread.isRunning())
        {
            thread.start();
        }
        thread.quit();
        thread.wait();
    }

    QThread thread;
    image_loader* loader;
};

class pool_blocker
{
   public:
    pool_blocker() : max_threads_(QThreadPool::globalInstance()->maxThreadCount())
    {
        QThreadPool::globalInstance()->setMaxThreadCount(1);
        future_ = QtConcurrent::run([this]()
        {
            started_.release();
            gate_.acquire();
        });
        started_.acquire();
    }

    ~pool_blocker()
    {
        unblock();
        future_.waitForFinished();
        QThreadPool::globalInstance()->setMaxThreadCount(max_threads_);
    }

    void unblock()
    {
        if (!released_)
        {
            released_ = true;
            gate_.release();
        }
    }

   private:
    int max_threads_;
    bool released_ = false;
    QSemaphore started_;
    QSemaphore gate_;
    QFuture<void> future_;
};

class correctness_test : public QObject
{
    Q_OBJECT

   private slots:
    void large_visible_batch();
    void queued_cache_clear();
    void relayout_preserves_image_data();
    void relayout_preserves_image();
    void relayout_keeps_anchor();
    void dpr_refresh_keeps_image();
    void dpr_reuses_existing_pixels();
    void item_reuse_resets_image();
    void latest_layout_width_data();
    void latest_layout_width();
    void no_background_preload();
    void stale_queued_scans();
    void exif_orientation_data();
    void exif_orientation();
    void thumbnail_file_replaced();
    void viewer_file_replaced_data();
    void viewer_file_replaced();
    void movie_stops_on_close();
    void tall_image_stays_visible();
    void item_geometry_data();
    void item_geometry();
    void main_window_switches_folders();
    void fast_navigation();
    void large_image_and_close();
    void loader_cancel_and_clear();
    void scanner_cancel_during_batch();
    void panorama_is_requested_data();
    void panorama_is_requested();
    void loader_failure_finishes();
    void thumbnail_source_counts();
    void foreground_is_serial();
    void navigation_focus_data();
    void navigation_focus();
    void catalog_snapshot_is_shared();
    void thumbnail_budget_and_bound();
    void visible_requests_first();
    void disk_cache_budget();
    void thumbnail_alpha_roundtrip();
    void main_close_closes_viewer();
    void scanner_reports_root_error();
    void theme_keeps_scene_items();
    void theme_styles_menus_and_scrollbars();
    void scroll_updates_during_motion();
    void viewer_error_is_visible();
    void scanner_sort_modes_data();
    void scanner_sort_modes();
    void clear_resets_scroll_range();
    void reprioritize_after_scroll();
    void same_path_replaced_inflight();
    void layout_result_is_consumed();
    void natural_sort_in_c_locale();
    void viewer_snapshot_is_shared();
    void scanner_formats_data();
    void scanner_formats();
    void jpeg_cache_roundtrip();
    void only_visible_requests();
    void scrolled_result_skips_old();
    void viewer_status_tracks_loading();
    void desktop_interactions();
    void trash_fresh_data_home();
    void thumbnail_native_size_data();
    void thumbnail_native_size();
    void item_uses_smooth_transform();
    void viewer_keeps_window_geometry();
    void viewer_rotation_shortcut();
    void open_selected_with_keyboard_data();
    void open_selected_with_keyboard();
    void viewer_mouse_navigation();
    void viewer_wheel_zoom_data();
    void viewer_wheel_zoom();
    void compact_sort_controls();
    void compact_preview_controls();
    void custom_header_data();
    void custom_header();
    void header_follows_theme();
    void popup_indicators_data();
    void popup_indicators();
    void directory_context();
    void toolbar_uses_text_buttons();
    void card_focus_state();
    void scene_focus_tracks_path();
    void recent_menu_is_flat();
    void sorting_is_in_toolbar();
    void xcb_header_gestures();
};

void correctness_test::large_visible_batch()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    QImage image(200, 2, QImage::Format_RGB32);
    image.fill(Qt::red);

    QList<image_meta> models;
    for (int i = 0; i < 240; ++i)
    {
        const QString path = dir.filePath(QString::number(i) + ".png");
        QVERIFY(image.save(path));
        models.append({path, image.size()});
    }

    loader_worker worker;
    waterfall_scene scene;
    QGraphicsView view(&scene);
    view.resize(440, 2000);
    view.show();
    QTRY_VERIFY(view.viewport()->height() > 1900);
    QSignalSpy loaded(worker.loader, &image_loader::thumbnail_loaded);
    connect(worker.loader, &image_loader::thumbnail_loaded, &scene, &waterfall_scene::on_image_loaded);

    int submissions = 0;
    connect(&scene, &waterfall_scene::request_load_batch, &scene, [&](const QList<load_task>& tasks)
    {
        ++submissions;
        worker.loader->request_thumbnails(tasks);
    });
    scene.add_images(models);
    scene.layout_models(440);
    QTRY_VERIFY(submissions > 0);
    scene.update_viewport(QRectF(0, 0, 440, 2000));
    QCOMPARE(submissions, 1);

    worker.thread.start();
    QTRY_COMPARE(loaded.size(), models.size());
}

void correctness_test::queued_cache_clear()
{
    const QString marker = QStandardPaths::writableLocation(QStandardPaths::CacheLocation) + "/thumbnails/marker.png";
    loader_worker worker;
    QFile file(marker);
    QVERIFY(file.open(QIODevice::WriteOnly));
    file.close();

    worker.thread.start();
    QVERIFY(QMetaObject::invokeMethod(worker.loader, &image_loader::clear_cache, Qt::QueuedConnection));
    QTRY_VERIFY_WITH_TIMEOUT(!QFileInfo::exists(marker), 500);
}

void correctness_test::relayout_preserves_image_data()
{
    QTest::addColumn<bool>("append_batch");
    QTest::newRow("resize") << false;
    QTest::newRow("append-during-resize") << true;
}

void correctness_test::relayout_preserves_image()
{
    QFETCH(bool, append_batch);
    waterfall_scene scene;
    QGraphicsView view(&scene);
    QSignalSpy requests(&scene, &waterfall_scene::request_load_batch);
    scene.add_images({{"first.png", QSize(100, 50)}});
    scene.layout_models(440);
    QTRY_COMPARE(scene.sceneRect().width(), 440);
    scene.update_viewport(QRectF(0, 0, 440, 600));
    QVERIFY(!requests.isEmpty());
    const auto task = qvariant_cast<QList<load_task>>(requests.first().first()).first();
    QImage image(100, 50, QImage::Format_RGB32);
    image.fill(Qt::red);
    scene.on_image_loaded(task.id, task.path, image, task.session_id);

    waterfall_item* item = nullptr;
    for (auto* candidate : scene.items())
    {
        auto* thumbnail = dynamic_cast<waterfall_item*>(candidate);
        if (thumbnail != nullptr && thumbnail->isVisible() && thumbnail->get_index() == 0)
        {
            item = thumbnail;
            break;
        }
    }
    QVERIFY(item != nullptr);
    pool_blocker blocker;
    scene.layout_models(640);
    if (append_batch)
    {
        scene.add_images({{"second.png", QSize(100, 50)}});
    }
    blocker.unblock();
    QTRY_COMPARE(scene.sceneRect().width(), 640);
    QCOMPARE(item->pixmap().toImage().pixelColor(0, 0), QColor(Qt::red));
    QCOMPARE(item->mapRectToScene(QRectF(item->pixmap().rect())).width(), 305);
}

void correctness_test::relayout_keeps_anchor()
{
    waterfall_scene scene;
    waterfall_view view;
    view.setScene(&scene);
    connect(&view, &waterfall_view::view_resized, &scene, &waterfall_scene::layout_models);
    view.resize(800, 600);
    view.show();
    QList<image_meta> images;
    for (int index = 0; index < 2000; ++index)
    {
        images.append({QString::number(index) + ".jpg", QSize(400, 200 + index % 7 * 100)});
    }
    scene.add_images(images);
    scene.layout_models(view.viewport()->width());
    QTRY_COMPARE(scene.last_layout_index_, size_t(2000));
    QTest::qWait(200);
    QVERIFY(scene.focus_path("1000.jpg"));
    scene.clearSelection();
    QTest::qWait(40);
    for (int width : {1150, 620, 800})
    {
        const QRectF visible = view.mapToScene(view.viewport()->rect()).boundingRect();
        int anchor = -1;
        qreal distance = std::numeric_limits<qreal>::max();
        for (auto it = scene.active_items_.cbegin(); it != scene.active_items_.cend(); ++it)
        {
            const QRectF rect = scene.all_models_[it.key()].layout_rect;
            const qreal candidate = std::abs(rect.center().y() - visible.center().y()) * 10000 +
                                    std::abs(rect.center().x() - visible.center().x());
            if (rect.intersects(visible) && candidate < distance)
            {
                anchor = it.key();
                distance = candidate;
            }
        }
        QVERIFY(anchor >= 0);
        const QRectF old_rect = scene.all_models_[anchor].layout_rect;
        const qreal fraction = std::clamp((visible.center().y() - old_rect.top()) / old_rect.height(), 0.0, 1.0);
        const qreal relative = (old_rect.top() + fraction * old_rect.height() - visible.top()) / visible.height();
        view.resize(width, 600);
        QTest::qWait(220);
        QTRY_VERIFY(!scene.is_laying_out_);
        const QRectF after = view.mapToScene(view.viewport()->rect()).boundingRect();
        const QRectF rect = scene.all_models_[anchor].layout_rect;
        const qreal actual = (rect.top() + fraction * rect.height() - after.top()) / after.height();
        QVERIFY2(std::abs(actual - relative) < 0.02, qPrintable(QString("image=%1 relative=%2 actual=%3 width=%4").arg(anchor).arg(relative).arg(actual).arg(width)));
    }
}

void correctness_test::dpr_refresh_keeps_image()
{
    waterfall_scene scene;
    scaled_view view;
    view.setScene(&scene);
    view.resize(500, 400);
    view.show();
    QSignalSpy requested(&scene, &waterfall_scene::request_load_batch);
    scene.add_images({{"photo.jpg", QSize(4000, 3000)}});
    scene.layout_models(view.viewport()->width());
    QTRY_COMPARE(requested.size(), 1);
    auto task = qvariant_cast<QList<load_task>>(requested.last()[0]).first();
    QImage image(task.target_size, QImage::Format_RGB32);
    image.fill(Qt::green);
    scene.on_image_loaded(task.id, task.path, image, task.session_id);
    auto* item = scene.active_items_[0];
    const qint64 old_key = item->pixmap().cacheKey();
    for (qreal ratio : {1.5, 2.0})
    {
        const int previous = requested.size();
        view.ratio = ratio;
        QCOMPARE(view.devicePixelRatioF(), ratio);
        QEvent event(QEvent::ScreenChangeInternal);
        QApplication::sendEvent(&view, &event);
        QTRY_COMPARE(requested.size(), previous + 1);
        task = qvariant_cast<QList<load_task>>(requested.last()[0]).first();
        QCOMPARE(task.target_size.width(), qRound(scene.all_models_[0].layout_rect.width() * ratio));
        QVERIFY(!item->pixmap().isNull());
        if (ratio == 1.5) { QCOMPARE(item->pixmap().cacheKey(), old_key); }
        const qint64 retained_key = item->pixmap().cacheKey();
        QImage next;
        if (ratio == 1.5)
        {
            next = QImage(task.target_size, QImage::Format_RGB32);
            next.fill(Qt::green);
        }
        scene.on_image_loaded(task.id, task.path, next, task.session_id);
        if (ratio == 2)
        {
            QCOMPARE(item->pixmap().cacheKey(), retained_key);
            QCOMPARE(item->state(), waterfall_item::load_state::ready);
        }
    }
    const int previous = requested.size();
    view.ratio = 1;
    QEvent event(QEvent::ScreenChangeInternal);
    QApplication::sendEvent(&view, &event);
    QTest::qWait(50);
    QCOMPARE(requested.size(), previous);
    QCOMPARE(item->state(), waterfall_item::load_state::ready);
}

void correctness_test::dpr_reuses_existing_pixels()
{
    waterfall_scene scene;
    scaled_view view;
    view.setScene(&scene);
    view.resize(400, 400);
    view.show();
    scene.add_images({{"photo.jpg", QSize(4000, 3000)}});
    QSignalSpy requested(&scene, &waterfall_scene::request_load_batch);
    scene.layout_models(view.viewport()->width());
    QTRY_COMPARE(requested.size(), 1);
    auto task = qvariant_cast<QList<load_task>>(requested.last()[0]).first();
    QImage image(task.target_size, QImage::Format_RGB32);
    image.fill(Qt::blue);
    scene.on_image_loaded(task.id, task.path, image, task.session_id);
    const qint64 key = scene.active_items_[0]->pixmap().cacheKey();
    view.resize(260, 400);
    scene.layout_models(view.viewport()->width());
    QTRY_COMPARE(qRound(scene.sceneRect().width()), view.viewport()->width());
    view.ratio = 1.5;
    QEvent changed(QEvent::ScreenChangeInternal);
    QApplication::sendEvent(&view, &changed);
    QTest::qWait(40);
    QVERIFY(image.width() >= qRound(scene.all_models_[0].layout_rect.width() * view.ratio));
    QCOMPARE(requested.size(), 1);
    QCOMPARE(scene.active_items_[0]->pixmap().cacheKey(), key);
    view.ratio = 2;
    QApplication::sendEvent(&view, &changed);
    QTRY_COMPARE(requested.size(), 2);
    task = qvariant_cast<QList<load_task>>(requested.last()[0]).first();
    QCOMPARE(task.target_size.width(), qRound(scene.all_models_[0].layout_rect.width() * view.ratio));
}

void correctness_test::item_reuse_resets_image()
{
    waterfall_item item;
    layout_model model{QSize(100, 50), QRectF(10, 10, 200, 100)};
    item.bind_model(model, 0, "first.png", 200, 1);
    QPixmap image(100, 50);
    image.fill(Qt::red);
    item.set_pixmap_safe(image);
    item.reset();
    item.bind_model(model, 0, "second.png", 200, 2);
    QCOMPARE(item.pixmap().toImage().pixelColor(0, 0), QColor(Qt::lightGray));
}

void correctness_test::latest_layout_width_data()
{
    QTest::addColumn<QString>("sequence");
    QTest::newRow("repeat-then-new-width") << QString("repeat");
    QTest::newRow("return-to-applied-width") << QString("return");
    QTest::newRow("finished-before-notification") << QString("notification");
    QTest::newRow("clear-during-layout") << QString("clear");
}

void correctness_test::latest_layout_width()
{
    QFETCH(QString, sequence);
    waterfall_scene scene;
    scene.add_images({{"first.png", QSize(100, 50)}});
    scene.layout_models(440);
    QTRY_COMPARE(scene.sceneRect().width(), 440);

    pool_blocker blocker;
    scene.layout_models(640);
    if (sequence == "return")
    {
        scene.layout_models(440);
    }
    else if (sequence == "clear")
    {
        scene.clear_items();
        scene.add_images({{"second.png", QSize(50, 100)}});
        scene.layout_models(840);
    }
    else if (sequence == "repeat")
    {
        scene.layout_models(640);
    }
    blocker.unblock();
    QVERIFY(QThreadPool::globalInstance()->waitForDone(5000));
    if (sequence == "notification")
    {
        scene.layout_models(840);
    }
    QCoreApplication::sendPostedEvents();
    QCoreApplication::processEvents();

    if (sequence == "return")
    {
        QCOMPARE(scene.sceneRect().width(), 440);
        return;
    }
    if (sequence == "repeat")
    {
        QTRY_COMPARE(scene.sceneRect().width(), 640);
        scene.layout_models(840);
    }
    QTRY_COMPARE_WITH_TIMEOUT(scene.sceneRect().width(), 840, 500);
}

void correctness_test::no_background_preload()
{
    QTemporaryDir dir;
    QStringList paths;
    for (int i = 0; i < 3; ++i)
    {
        QImage image(100, 50, QImage::Format_RGB32);
        image.fill(Qt::blue);
        paths.append(dir.filePath(QString::number(i) + ".png"));
        QVERIFY(image.save(paths.back()));
    }
    image_viewer_window viewer;
    viewer.set_image_list(paths);
    viewer.set_image_path(paths[1]);
    QTRY_VERIFY(viewer.windowTitle().startsWith("Viewer -"));
    QCOMPARE(viewer.findChildren<QFutureWatcherBase*>().size(), 1);
    viewer.show();
    QVERIFY(viewer.close());
    viewer.show();
    viewer.set_image_list(paths);
    viewer.set_image_path(paths[2]);
    QTRY_VERIFY(viewer.windowTitle().contains("2.png (100x50)"));
    QCOMPARE(viewer.findChildren<QFutureWatcherBase*>().size(), 1);
}

void correctness_test::stale_queued_scans()
{
    QTemporaryDir dir;
    QImage image(100, 50, QImage::Format_RGB32);
    image.fill(Qt::red);
    QVERIFY(image.save(dir.filePath("image.png")));

    file_scanner scanner;
    QSignalSpy finished(&scanner, &file_scanner::scan_finished);
    QSignalSpy batches(&scanner, &file_scanner::images_scanned_batch);
    for (int session = 1; session <= 3; ++session)
    {
        scanner.stop_scan();
        QMetaObject::invokeMethod(&scanner, [&, session]()
        {
            scanner.start_scan(dir.path(), session, 0, false);
        }, Qt::QueuedConnection);
    }
    QTRY_VERIFY(!finished.isEmpty());
    QCOMPARE(finished.size(), 1);
    QCOMPARE(finished.first()[2].toInt(), 3);
    QCOMPARE(batches.size(), 1);
    QCOMPARE(batches.first()[1].toInt(), 3);
}

void correctness_test::exif_orientation_data()
{
    QTest::addColumn<int>("orientation");
    for (int i = 1; i <= 8; ++i)
    {
        QTest::newRow(qPrintable(QString::number(i))) << i;
    }
}

void correctness_test::exif_orientation()
{
    QFETCH(int, orientation);
    QTemporaryDir dir;
    QImage source(120, 60, QImage::Format_RGB32);
    for (int y = 0; y < source.height(); ++y)
    {
        for (int x = 0; x < source.width(); ++x)
        {
            const QColor color = y < 30 ? (x < 60 ? Qt::red : Qt::green) : (x < 60 ? Qt::blue : Qt::yellow);
            source.setPixelColor(x, y, color);
        }
    }
    QByteArray jpeg;
    QBuffer buffer(&jpeg);
    QVERIFY(buffer.open(QIODevice::WriteOnly));
    QVERIFY(source.save(&buffer, "JPEG", 100));
    QByteArray exif = QByteArray::fromHex("ffe1002245786966000049492a000800000001001201030001000000");
    exif.append(static_cast<char>(orientation));
    exif.append(QByteArray(7, '\0'));
    jpeg.insert(2, exif);
    const QString path = dir.filePath("oriented.jpg");
    QFile file(path);
    QVERIFY(file.open(QIODevice::WriteOnly));
    QCOMPARE(file.write(jpeg), jpeg.size());
    file.close();

    QImageReader reader(path);
    reader.setAutoTransform(true);
    const QImage expected = reader.read();
    QVERIFY(!expected.isNull());
    const QSize display_size = orientation >= 5 ? QSize(60, 120) : QSize(120, 60);
    QCOMPARE(expected.size(), display_size);

    file_scanner scanner;
    QSignalSpy batches(&scanner, &file_scanner::images_scanned_batch);
    scanner.start_scan(dir.path(), scanner.stop_scan(), 0, false);
    QCOMPARE(batches.size(), 1);
    const auto metadata = qvariant_cast<QList<image_meta>>(batches.first().first());
    QCOMPARE(metadata.first().original_size, display_size);

    for (int pass = 0; pass < 2; ++pass)
    {
        loader_worker worker;
        QSignalSpy loaded(worker.loader, &image_loader::thumbnail_loaded);
        const QSize target_size = display_size * (2.0 / 3.0);
        worker.loader->request_thumbnails({{1, path, target_size, 1}});
        worker.thread.start();
        QTRY_COMPARE(loaded.size(), 1);
        const QImage thumbnail = qvariant_cast<QImage>(loaded.first()[2]);
        QCOMPARE(thumbnail.size(), target_size);
        for (const QPoint& corner : {QPoint(0, 0), QPoint(1, 0), QPoint(0, 1), QPoint(1, 1)})
        {
            const QColor actual = thumbnail.pixelColor(thumbnail.width() * (1 + 2 * corner.x()) / 4,
                                                       thumbnail.height() * (1 + 2 * corner.y()) / 4);
            const QColor reference = expected.pixelColor(expected.width() * (1 + 2 * corner.x()) / 4,
                                                         expected.height() * (1 + 2 * corner.y()) / 4);
            QVERIFY(std::abs(actual.red() - reference.red()) < 10);
            QVERIFY(std::abs(actual.green() - reference.green()) < 10);
            QVERIFY(std::abs(actual.blue() - reference.blue()) < 10);
        }
    }
}

void correctness_test::thumbnail_file_replaced()
{
    QTemporaryDir dir;
    const QString path = dir.filePath("image.png");
    QImage image(100, 50, QImage::Format_RGB32);
    image.fill(Qt::red);
    QVERIFY(image.save(path));
    const QDateTime modified = QFileInfo(path).lastModified();
    {
        loader_worker worker;
        QSignalSpy loaded(worker.loader, &image_loader::thumbnail_loaded);
        worker.thread.start();
        worker.loader->request_thumbnails({{1, path, QSize(50, 25), 1}});
        QTRY_COMPARE(loaded.size(), 1);
        QCOMPARE(qvariant_cast<QImage>(loaded.first()[2]).pixelColor(0, 0), QColor(Qt::red));

        image.fill(Qt::blue);
        QVERIFY(image.save(path));
        QFile file(path);
        QVERIFY(file.open(QIODevice::ReadWrite));
        QVERIFY(file.setFileTime(modified.addSecs(2), QFileDevice::FileModificationTime));
        file.close();
        worker.loader->request_thumbnails({{2, path, QSize(50, 25), 1}});
        QTRY_COMPARE(loaded.size(), 2);
        QCOMPARE(qvariant_cast<QImage>(loaded.last()[2]).pixelColor(0, 0), QColor(Qt::blue));
    }
    loader_worker worker;
    QSignalSpy loaded(worker.loader, &image_loader::thumbnail_loaded);
    worker.thread.start();
    worker.loader->request_thumbnails({{3, path, QSize(50, 25), 1}});
    QTRY_COMPARE(loaded.size(), 1);
    QCOMPARE(qvariant_cast<QImage>(loaded.first()[2]).pixelColor(0, 0), QColor(Qt::blue));
}

void correctness_test::viewer_file_replaced_data()
{
    QTest::addColumn<bool>("reopen");
    QTest::newRow("same-window") << false;
    QTest::newRow("after-close") << true;
}

void correctness_test::viewer_file_replaced()
{
    QFETCH(bool, reopen);
    QTemporaryDir dir;
    const QString path = dir.filePath("image.png");
    const QString neighbor = dir.filePath("neighbor.png");
    QImage image(100, 50, QImage::Format_RGB32);
    image.fill(Qt::red);
    QVERIFY(image.save(path));
    image.fill(Qt::green);
    QVERIFY(image.save(neighbor));
    const QDateTime modified = QFileInfo(path).lastModified();
    image_viewer_window viewer;
    viewer.set_image_list({neighbor, path});
    viewer.set_image_path(path);
    QTRY_VERIFY(viewer.windowTitle().startsWith("Viewer -"));
    if (reopen)
    {
        viewer.show();
        QVERIFY(viewer.close());
        viewer.show();
    }

    image.fill(Qt::blue);
    QVERIFY(image.save(path));
    QFile file(path);
    QVERIFY(file.open(QIODevice::ReadWrite));
    QVERIFY(file.setFileTime(modified.addSecs(2), QFileDevice::FileModificationTime));
    file.close();
    viewer.set_image_path(path);
    auto* scene = viewer.findChild<QGraphicsView*>()->scene();
    const auto shows_new_image = [scene]()
    {
        if (scene->items().size() != 1)
        {
            return false;
        }
        auto* item = dynamic_cast<QGraphicsPixmapItem*>(scene->items().first());
        return item != nullptr && item->pixmap().toImage().pixelColor(0, 0) == QColor(Qt::blue);
    };
    QTRY_VERIFY_WITH_TIMEOUT(shows_new_image(), 500);
}

void correctness_test::movie_stops_on_close()
{
    QTemporaryDir dir;
    const QString path = dir.filePath("animated.gif");
    QFile file(path);
    QVERIFY(file.open(QIODevice::WriteOnly));
    const QByteArray gif = QByteArray::fromBase64(kAnimatedGif);
    QCOMPARE(file.write(gif), gif.size());
    file.close();

    image_viewer_window viewer;
    viewer.show();
    viewer.set_image_path(path);
    QPointer<QMovie> movie = viewer.findChild<QMovie*>();
    QVERIFY(!movie.isNull());
    QCOMPARE(movie->frameCount(), 2);
    QCOMPARE(movie->cacheMode(), QMovie::CacheNone);
    QCOMPARE(movie->state(), QMovie::Running);
    QVERIFY(viewer.close());
    QVERIFY(movie.isNull() || movie->state() == QMovie::NotRunning);
    QTRY_VERIFY(movie.isNull());

    viewer.show();
    viewer.set_image_path(path);
    movie = viewer.findChild<QMovie*>();
    QVERIFY(!movie.isNull());
    QCOMPARE(movie->state(), QMovie::Running);
    const QString still_path = dir.filePath("still.png");
    QImage image(100, 50, QImage::Format_RGB32);
    image.fill(Qt::green);
    QVERIFY(image.save(still_path));
    viewer.set_image_path(still_path);
    QTRY_VERIFY(movie.isNull());
    QTRY_VERIFY(viewer.windowTitle().contains("still.png (100x50)"));
}

void correctness_test::tall_image_stays_visible()
{
    waterfall_scene scene;
    QList<image_meta> models = {{"tall.png", QSize(100, 10000)}};
    for (int i = 0; i < 80; ++i)
    {
        models.append({QString::number(i) + ".png", QSize(100, 100)});
    }
    scene.add_images(models);
    scene.layout_models(440);
    QTRY_COMPARE(scene.sceneRect().width(), 440);
    scene.update_viewport(QRectF(0, 0, 440, 400));
    scene.update_viewport(QRectF(0, 9000, 440, 400));

    bool tall_visible = false;
    bool first_short_visible = false;
    for (auto* candidate : scene.items())
    {
        auto* item = dynamic_cast<waterfall_item*>(candidate);
        if (item == nullptr || !item->isVisible())
        {
            continue;
        }
        tall_visible |= item->get_path() == "tall.png";
        first_short_visible |= item->get_index() == 1;
    }
    QVERIFY(tall_visible);
    QVERIFY(!first_short_visible);
}

void correctness_test::item_geometry_data()
{
    QTest::addColumn<QSize>("pixel_size");
    QTest::newRow("upscale") << QSize(100, 50);
    QTest::newRow("downscale") << QSize(400, 200);
}

void correctness_test::item_geometry()
{
    QFETCH(QSize, pixel_size);
    QGraphicsScene scene;
    waterfall_item item;
    scene.addItem(&item);
    const QRectF expected(10, 20, 200, 100);
    item.bind_model({QSize(100, 50), expected}, 0, "image.png", 200, 1);
    QPixmap image(pixel_size);
    image.fill(Qt::red);
    item.set_pixmap_safe(image);
    const QRectF pixels(item.pixmap().rect());
    QCOMPARE(item.mapRectToScene(pixels), expected);

    QGraphicsSceneHoverEvent enter(QEvent::GraphicsSceneHoverEnter);
    QVERIFY(scene.sendEvent(&item, &enter));
    const QRectF hovered = item.mapRectToScene(pixels);
    QCOMPARE(hovered.center(), expected.center());
    QCOMPARE(hovered.width(), expected.width());
    QGraphicsSceneHoverEvent leave(QEvent::GraphicsSceneHoverLeave);
    QVERIFY(scene.sendEvent(&item, &leave));
    QCOMPARE(item.mapRectToScene(pixels), expected);
}

void correctness_test::main_window_switches_folders()
{
    QTemporaryDir dir;
    QStringList folders;
    for (int i = 0; i < 3; ++i)
    {
        const QString folder = dir.filePath(QString::number(i));
        QVERIFY(QDir().mkpath(folder));
        QImage image(100, 50, QImage::Format_RGB32);
        image.fill(Qt::blue);
        QVERIFY(image.save(folder + "/image10.png"));
        QVERIFY(image.save(folder + "/image2.png"));
        folders.push_back(folder);
    }
    main_window window;
    window.show();
    auto* scene = window.findChild<waterfall_scene*>();
    QVERIFY(scene != nullptr);
    for (const QString& folder : folders)
    {
        QMimeData mime;
        mime.setUrls({QUrl::fromLocalFile(folder)});
        QDragEnterEvent enter(QPoint(1, 1), Qt::CopyAction, &mime, Qt::LeftButton, Qt::NoModifier);
        QApplication::sendEvent(&window, &enter);
        QVERIFY(enter.isAccepted());
        QDropEvent drop(QPointF(1, 1), Qt::CopyAction, &mime, Qt::LeftButton, Qt::NoModifier);
        QApplication::sendEvent(&window, &drop);
        QVERIFY(drop.isAccepted());
    }
    const QStringList expected = {folders.back() + "/image2.png", folders.back() + "/image10.png"};
    QTRY_COMPARE(scene->get_all_paths().size(), expected.size());
    QCOMPARE(scene->get_all_paths()[0], expected[0]);
    QCOMPARE(scene->get_all_paths()[1], expected[1]);
    const auto loaded_latest = [scene]()
    {
        int loaded = 0;
        for (auto* candidate : scene->items())
        {
            auto* item = dynamic_cast<waterfall_item*>(candidate);
            if (item != nullptr && item->isVisible() && item->pixmap().toImage().pixelColor(0, 0) == QColor(Qt::blue))
            {
                ++loaded;
            }
        }
        return loaded == 2;
    };
    QTRY_VERIFY(loaded_latest());
    QVERIFY(window.close());
}

void correctness_test::fast_navigation()
{
    QTemporaryDir dir;
    QStringList paths;
    for (int i = 0; i < 3; ++i)
    {
        QImage image(100 + i, 50, QImage::Format_RGB32);
        image.fill(i == 2 ? Qt::blue : Qt::red);
        paths.push_back(dir.filePath(QString::number(i) + ".png"));
        QVERIFY(image.save(paths.back()));
    }
    image_viewer_window viewer;
    viewer.set_image_list(paths);
    pool_blocker blocker;
    for (int i = 0; i < 30; ++i)
    {
        viewer.set_image_path(paths[i % paths.size()]);
    }
    blocker.unblock();
    QTRY_VERIFY(viewer.windowTitle().contains("2.png (102x50)"));
    QVERIFY(QThreadPool::globalInstance()->waitForDone(5000));
    QCoreApplication::sendPostedEvents();
    auto* scene = viewer.findChild<QGraphicsView*>()->scene();
    QCOMPARE(scene->items().size(), 1);
    auto* item = dynamic_cast<QGraphicsPixmapItem*>(scene->items().first());
    QVERIFY(item != nullptr);
    QCOMPARE(item->pixmap().toImage().pixelColor(0, 0), QColor(Qt::blue));

    const QString gif_path = dir.filePath("animated.gif");
    QFile gif(gif_path);
    QVERIFY(gif.open(QIODevice::WriteOnly));
    QVERIFY(gif.write(QByteArray::fromBase64(kAnimatedGif)) > 0);
    gif.close();
    {
        pool_blocker pending_load;
        viewer.set_image_path(paths[0]);
        viewer.set_image_path(gif_path);
        pending_load.unblock();
        QVERIFY(QThreadPool::globalInstance()->waitForDone(5000));
        QCoreApplication::sendPostedEvents();
        QCOMPARE(viewer.current_image_path(), gif_path);
        QVERIFY(viewer.windowTitle().contains("animated.gif (2x2)"));
        QCOMPARE(scene->items().size(), 1);
    }

    viewer.show();
    viewer.set_image_path(paths[0]);
    QVERIFY(viewer.close());
    QCOMPARE(viewer.current_image_path(), QString());
    QVERIFY(scene->items().isEmpty());
    viewer.show();
    viewer.set_image_path(paths[2]);
    QTRY_VERIFY(viewer.windowTitle().contains("2.png (102x50)"));
    QCOMPARE(scene->items().size(), 1);
}

void correctness_test::large_image_and_close()
{
    QTemporaryDir dir;
    const QString path = dir.filePath("large.jpg");
    {
        QImage source(9000, 8000, QImage::Format_RGB32);
        QVERIFY(!source.isNull());
        source.fill(Qt::red);
        QVERIFY(source.save(path, "JPEG", 80));
    }
    auto* viewer = new image_viewer_window();
    viewer->set_image_path(path);
    QTRY_VERIFY_WITH_TIMEOUT(viewer->windowTitle().startsWith("Viewer -"), 15000);
    auto* scene = viewer->findChild<QGraphicsView*>()->scene();
    QCOMPARE(scene->items().size(), 1);
    auto* item = dynamic_cast<QGraphicsPixmapItem*>(scene->items().first());
    QVERIFY(item != nullptr);
    QVERIFY(item->pixmap().width() < 9000);
    QVERIFY(item->pixmap().width() <= 7875);
    QVERIFY(item->pixmap().height() < 8000);
    {
        const QImage decoded = item->pixmap().toImage();
        QVERIFY(decoded.sizeInBytes() <= (kMaxImageAllocMB + 1LL) * 1024 * 1024);
        QVERIFY(decoded.pixelColor(0, 0).red() > 240);
    }
    QPointer<image_viewer_window> lifetime = viewer;
    delete viewer;
    QVERIFY(lifetime.isNull());

    viewer = new image_viewer_window();
    viewer->show();
    viewer->set_image_path(path);
    QVERIFY(viewer->close());
    QCOMPARE(viewer->current_image_path(), QString());
    QVERIFY(viewer->findChild<QGraphicsView*>()->scene()->items().isEmpty());
    delete viewer;
}

void correctness_test::loader_cancel_and_clear()
{
    QTemporaryDir dir;
    QImage image(100, 50, QImage::Format_RGB32);
    image.fill(Qt::red);
    const QString first = dir.filePath("first.png");
    const QString second = dir.filePath("second.png");
    QVERIFY(image.save(first));
    QVERIFY(image.save(second));
    loader_worker worker;
    QSignalSpy loaded(worker.loader, &image_loader::thumbnail_loaded);
    worker.loader->request_thumbnails({{1, first, QSize(50, 25), 1}, {2, second, QSize(50, 25), 1}});
    worker.loader->cancel_thumbnails({first});
    worker.thread.start();
    QTRY_COMPARE(loaded.size(), 1);
    QCOMPARE(loaded.first().first().toULongLong(), 2);
    worker.loader->request_thumbnails({{3, first, QSize(50, 25), 1}});
    QTRY_COMPARE(loaded.size(), 2);
    QCOMPARE(loaded.last().first().toULongLong(), 3);
    QVERIFY(QMetaObject::invokeMethod(worker.loader, &image_loader::clear_cache, Qt::QueuedConnection));
    worker.loader->clear_all();
    worker.loader->request_thumbnails({{4, second, QSize(50, 25), 2}});
    QTRY_COMPARE(loaded.size(), 3);
    QCOMPARE(loaded.last().first().toULongLong(), 4);
}

void correctness_test::scanner_cancel_during_batch()
{
    QTemporaryDir dir;
    QImage image(2, 2, QImage::Format_RGB32);
    image.fill(Qt::blue);
    for (int i = 0; i < 501; ++i)
    {
        QVERIFY(image.save(dir.filePath(QString::number(i) + ".png")));
    }
    file_scanner scanner;
    QSignalSpy batches(&scanner, &file_scanner::images_scanned_batch);
    QSignalSpy finished(&scanner, &file_scanner::scan_finished);
    connect(&scanner, &file_scanner::images_scanned_batch, &scanner, &file_scanner::stop_scan);
    scanner.start_scan(dir.path(), scanner.stop_scan(), 0, false);
    QCOMPARE(batches.size(), 1);
    QCOMPARE(qvariant_cast<QList<image_meta>>(batches.first().first()).size(), 500);
    QVERIFY(finished.isEmpty());
}

void correctness_test::panorama_is_requested_data()
{
    QTest::addColumn<bool>("append");
    QTest::newRow("full-layout") << false;
    QTest::newRow("incremental-layout") << true;
}

void correctness_test::panorama_is_requested()
{
    QFETCH(bool, append);
    waterfall_scene scene;
    QGraphicsView view(&scene);
    QSignalSpy requests(&scene, &waterfall_scene::request_load_batch);
    if (append)
    {
        scene.add_images({{"square.png", QSize(100, 100)}});
        scene.layout_models(440);
        QTRY_COMPARE(scene.sceneRect().width(), 440);
    }
    scene.add_images({{"panorama.png", QSize(30000, 100)}});
    scene.layout_models(440);
    QTRY_COMPARE(scene.sceneRect().width(), 440);
    scene.update_viewport(QRectF(0, 0, 440, 600));
    bool panorama_requested = false;
    load_task panorama_task{};
    for (const auto& request : requests)
    {
        for (const auto& task : qvariant_cast<QList<load_task>>(request.first()))
        {
            if (task.path == "panorama.png")
            {
                panorama_requested = true;
                panorama_task = task;
                QVERIFY(task.target_size.height() >= 1);
            }
        }
    }
    QVERIFY(panorama_requested);
    QImage thumbnail(panorama_task.target_size, QImage::Format_RGB32);
    thumbnail.fill(Qt::red);
    scene.on_image_loaded(panorama_task.id, panorama_task.path, thumbnail, panorama_task.session_id);
    bool visible = false;
    for (auto* candidate : scene.items())
    {
        auto* item = dynamic_cast<waterfall_item*>(candidate);
        if (item != nullptr && item->get_path() == "panorama.png" && item->isVisible())
        {
            visible = true;
            QVERIFY(item->mapRectToScene(QRectF(item->pixmap().rect())).height() >= 1);
            QCOMPARE(item->pixmap().toImage().pixelColor(0, 0), QColor(Qt::red));
        }
    }
    QVERIFY(visible);
}

void correctness_test::loader_failure_finishes()
{
    loader_worker worker;
    QSignalSpy loaded(worker.loader, &image_loader::thumbnail_loaded);
    worker.loader->request_thumbnails({{1, "/missing/imageviewer.png", QSize(100, 100), 7}});
    worker.thread.start();
    QTRY_COMPARE_WITH_TIMEOUT(loaded.size(), 1, 300);
    QCOMPARE(loaded.first()[3].toInt(), 7);
    QVERIFY(qvariant_cast<QImage>(loaded.first()[2]).isNull());
    QCOMPARE(qvariant_cast<image_loader::thumbnail_source>(loaded.first()[4]), image_loader::thumbnail_source::failed);
}

void correctness_test::thumbnail_source_counts()
{
    QTemporaryDir dir;
    const QString path = dir.filePath("source.png");
    QImage image(200, 150, QImage::Format_RGB32);
    image.fill(Qt::green);
    QVERIFY(image.save(path));
    {
        loader_worker worker;
        QSignalSpy started(worker.loader, &image_loader::thumbnail_started);
        QSignalSpy loaded(worker.loader, &image_loader::thumbnail_loaded);
        worker.thread.start();
        worker.loader->request_thumbnails({{1, path, QSize(80, 60), 9}});
        QTRY_COMPARE(loaded.size(), 1);
        QCOMPARE(qvariant_cast<image_loader::thumbnail_source>(loaded[0][4]), image_loader::thumbnail_source::decoded);
        worker.loader->request_thumbnails({{2, path, QSize(80, 60), 9}});
        QTRY_COMPARE(loaded.size(), 2);
        QCOMPARE(qvariant_cast<image_loader::thumbnail_source>(loaded[1][4]), image_loader::thumbnail_source::memory);
        QCOMPARE(started.size(), 2);
        QCOMPARE(started[1][0].toULongLong(), quint64(2));
        QCOMPARE(started[1][1].toInt(), 9);
    }
    loader_worker worker;
    QSignalSpy loaded(worker.loader, &image_loader::thumbnail_loaded);
    worker.thread.start();
    worker.loader->request_thumbnails({{3, path, QSize(80, 60), 9}});
    QTRY_COMPARE(loaded.size(), 1);
    QCOMPARE(qvariant_cast<image_loader::thumbnail_source>(loaded[0][4]), image_loader::thumbnail_source::disk);
}

void correctness_test::foreground_is_serial()
{
    QTemporaryDir dir;
    QStringList paths;
    QImage image(100, 50, QImage::Format_RGB32);
    image.fill(Qt::blue);
    for (int i = 0; i < 3; ++i)
    {
        paths.push_back(dir.filePath(QString::number(i) + ".png"));
        QVERIFY(image.save(paths.back()));
    }
    image_viewer_window viewer;
    pool_blocker blocker;
    viewer.set_image_path(paths[0]);
    auto* watcher = viewer.findChild<QFutureWatcherBase*>();
    QVERIFY(watcher != nullptr);
    QSignalSpy started(watcher, &QFutureWatcherBase::started);
    QCoreApplication::sendPostedEvents();
    viewer.set_image_path(paths[1]);
    QCoreApplication::sendPostedEvents();
    viewer.set_image_path(paths[2]);
    QCoreApplication::sendPostedEvents();
    const int submissions = started.size();
    blocker.unblock();
    QCOMPARE(submissions, 1);
    QTRY_VERIFY(viewer.windowTitle().contains("2.png (100x50)"));
}

void correctness_test::navigation_focus_data()
{
    QTest::addColumn<int>("key");
    QTest::addColumn<int>("expected");
    QTest::newRow("space") << int(Qt::Key_Space) << 2;
    QTest::newRow("backspace") << int(Qt::Key_Backspace) << 0;
    QTest::newRow("home") << int(Qt::Key_Home) << 0;
    QTest::newRow("end") << int(Qt::Key_End) << 2;
}

void correctness_test::navigation_focus()
{
    QFETCH(int, key);
    QFETCH(int, expected);
    image_viewer_window viewer;
    const QStringList paths = {"/missing/first.png", "/missing/middle.png", "/missing/last.png"};
    viewer.set_image_list(paths);
    viewer.set_image_path(paths[1]);
    viewer.show();
    auto* view = viewer.findChild<QGraphicsView*>();
    view->setFocus();
    QTest::keyClick(view, static_cast<Qt::Key>(key));
    QCOMPARE(viewer.current_image_path(), paths[expected]);
}

void correctness_test::catalog_snapshot_is_shared()
{
    waterfall_scene scene;
    scene.add_images({{"first.png", QSize(100, 50)}});
    const auto first = scene.get_all_paths();
    const auto second = scene.get_all_paths();
    QVERIFY(first.data() == second.data());
    scene.layout_models(440);
    QTRY_VERIFY(!scene.is_laying_out_);
    scene.update_viewport(QRectF(0, 0, 440, 400));
    const auto after_layout = scene.get_all_paths();
    QVERIFY(first.data() == after_layout.data());
    scene.clear_items();
    scene.add_images({{"second.png", QSize(50, 100)}});
    QCOMPARE(first.front(), QString("first.png"));
    QCOMPARE(scene.get_all_paths().front(), QString("second.png"));
}

void correctness_test::thumbnail_budget_and_bound()
{
    loader_worker worker;
    QList<load_task> tasks;
    for (int i = 0; i < 1000; ++i)
    {
        tasks.append({static_cast<quint64>(i + 1), QString("/missing/%1.png").arg(i), QSize(100, 100), 1});
    }
    worker.loader->request_thumbnails(tasks);
    QCOMPARE(worker.loader->cache_.maxCost(), 64LL * 1024 * 1024);
    QVERIFY(worker.loader->task_queue_.size() <= 64);
}

void correctness_test::visible_requests_first()
{
    waterfall_scene scene;
    QSignalSpy requests(&scene, &waterfall_scene::request_load_batch);
    QList<image_meta> images;
    for (int i = 0; i < 100; ++i)
    {
        images.append({QString::number(i), QSize(100, 100)});
    }
    scene.add_images(images);
    scene.layout_models(440);
    QTRY_COMPARE(scene.sceneRect().width(), 440);
    scene.update_viewport(QRectF(0, 1000, 440, 300));
    QVERIFY(!requests.isEmpty());
    const auto tasks = qvariant_cast<QList<load_task>>(requests.last().first());
    const int first = tasks.first().path.toInt();
    QVERIFY(first >= 8 && first <= 13);
    QVERIFY(tasks.size() <= 64);
}

void correctness_test::disk_cache_budget()
{
    image_loader prepare;
    prepare.clear_cache();
    const QString directory = QStandardPaths::writableLocation(QStandardPaths::CacheLocation) + "/thumbnails";
    QImage image(2, 2, QImage::Format_RGB32);
    image.fill(Qt::red);
    for (int i = 0; i < 1001; ++i)
    {
        const QString suffix = i % 2 == 0 ? "png" : "thumb";
        QVERIFY(image.save(directory + QString("/seed-%1.%2").arg(i).arg(suffix), "PNG"));
    }
    image_loader loader;
    QVERIFY(QDir(directory).entryList(QDir::Files).size() <= 1000);
    loader.clear_cache();
    QVERIFY(QDir(directory).entryList(QDir::Files).isEmpty());
}

void correctness_test::thumbnail_alpha_roundtrip()
{
    QTemporaryDir dir;
    const QString path = dir.filePath("alpha.png");
    QImage source(100, 50, QImage::Format_ARGB32_Premultiplied);
    source.fill(QColor(255, 0, 0, 128));
    QVERIFY(source.save(path));
    for (int pass = 0; pass < 2; ++pass)
    {
        loader_worker worker;
        QSignalSpy loaded(worker.loader, &image_loader::thumbnail_loaded);
        worker.loader->request_thumbnails({{1, path, source.size(), 1}});
        worker.thread.start();
        QTRY_COMPARE(loaded.size(), 1);
        QCOMPARE(qvariant_cast<QImage>(loaded.first()[2]).pixelColor(0, 0), QColor(255, 0, 0, 128));
    }
}

void correctness_test::main_close_closes_viewer()
{
    QTemporaryDir dir;
    const QString path = dir.filePath("image.png");
    QImage image(100, 50, QImage::Format_RGB32);
    image.fill(Qt::blue);
    QVERIFY(image.save(path));
    main_window window;
    window.show();
    QMimeData mime;
    mime.setUrls({QUrl::fromLocalFile(path)});
    QDragEnterEvent enter(QPoint(1, 1), Qt::CopyAction, &mime, Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(&window, &enter);
    QDropEvent drop(QPointF(1, 1), Qt::CopyAction, &mime, Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(&window, &drop);
    auto* viewer = window.findChild<image_viewer_window*>();
    QVERIFY(viewer != nullptr && viewer->isVisible());
    QVERIFY(window.close());
    QVERIFY(!viewer->isVisible());
    QVERIFY(viewer->current_image_path().isEmpty());
}

void correctness_test::scanner_reports_root_error()
{
    file_scanner scanner;
    QSignalSpy failed(&scanner, &file_scanner::scan_failed);
    scanner.start_scan("/missing/imageviewer-directory", scanner.stop_scan(), 0, false);
    QCOMPARE(failed.size(), 1);
#ifdef Q_OS_UNIX
    QTemporaryDir dir;
    QVERIFY(QFile::setPermissions(dir.path(), {}));
    scanner.start_scan(dir.path(), scanner.stop_scan(), 0, false);
    QVERIFY(QFile::setPermissions(dir.path(), QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner));
    QCOMPARE(failed.size(), 2);
#endif
}

void correctness_test::theme_keeps_scene_items()
{
    main_window window;
    auto* theme = window.findChild<QAction*>("dark_theme");
    QVERIFY(theme != nullptr);
    QVERIFY(theme->icon().isNull());
    QCOMPARE(theme->text(), QString("深色主题"));
    auto* scene = window.findChild<waterfall_scene*>();
    const int items = scene->items().size();
    theme->setChecked(true);
    QVERIFY(QApplication::palette().base().color().lightness() < 128);
    QCOMPARE(theme->text(), QString("浅色主题"));
    QCOMPARE(scene->items().size(), items);
    theme->setChecked(false);
    QVERIFY(QApplication::palette().base().color().lightness() > 128);
    QCOMPARE(theme->text(), QString("深色主题"));
    QCOMPARE(scene->items().size(), items);
}

void correctness_test::theme_styles_menus_and_scrollbars()
{
    main_window window;
    auto* theme = window.findChild<QAction*>("dark_theme");
    QVERIFY(theme != nullptr);
    theme->setChecked(false);
    theme->setChecked(true);
    QVERIFY(qApp->styleSheet().contains("padding: 6px 12px;"));

    QMenu menu;
    menu.addAction("排序");
    menu.show();
    QTRY_VERIFY(menu.isVisible());
    const QImage menu_image = menu.grab().toImage();
    const QColor dark_surface = QApplication::palette().color(QPalette::Base);
    QCOMPARE(menu_image.pixelColor(menu_image.width() - 8, menu_image.height() / 2).rgb(), dark_surface.rgb());

    QScrollBar scrollbar(Qt::Vertical);
    scrollbar.setRange(0, 100);
    scrollbar.setPageStep(10);
    scrollbar.resize(12, 120);
    scrollbar.show();
    QTRY_VERIFY(scrollbar.isVisible());
    const QImage scrollbar_image = scrollbar.grab().toImage();
    QCOMPARE(scrollbar_image.pixelColor(0, scrollbar_image.height() - 2).rgb(), dark_surface.rgb());

    QToolTip::showText(window.mapToGlobal(QPoint(24, 24)), "提示信息", &window);
    QTRY_VERIFY(QToolTip::isVisible());
    QWidget* tooltip = nullptr;
    for (auto* top_level : QApplication::topLevelWidgets())
    {
        if (top_level->windowType() == Qt::ToolTip)
        {
            tooltip = top_level;
            break;
        }
    }
    QVERIFY(tooltip != nullptr);
    const QImage tooltip_image = tooltip->grab().toImage();
    const QColor tooltip_surface = QApplication::palette().color(QPalette::ToolTipBase);
    QCOMPARE(tooltip_image.pixelColor(tooltip_image.width() - 4, tooltip_image.height() / 2).rgb(), tooltip_surface.rgb());
    QToolTip::hideText();
}

void correctness_test::scroll_updates_during_motion()
{
    waterfall_scene scene;
    waterfall_view view;
    view.setScene(&scene);
    view.resize(440, 600);
    view.show();
    QList<image_meta> images;
    for (int i = 0; i < 200; ++i)
    {
        images.append({QString::number(i), QSize(100, 100)});
    }
    scene.add_images(images);
    scene.layout_models(view.viewport()->width());
    QSignalSpy requests(&scene, &waterfall_scene::request_load_batch);
    QTRY_VERIFY(!requests.isEmpty());
    requests.clear();
    view.verticalScrollBar()->setValue(4000);
    QTest::qWait(40);
    QVERIFY(!requests.isEmpty());
}

void correctness_test::viewer_error_is_visible()
{
    QTemporaryDir dir;
    const QString path = dir.filePath("image.png");
    QImage image(600, 400, QImage::Format_RGB32);
    image.fill(Qt::blue);
    QVERIFY(image.save(path));
    image_viewer_window viewer;
    viewer.show();
    viewer.set_image_path(path);
    QTRY_VERIFY(viewer.windowTitle().startsWith("Viewer -"));
    for (int index = 0; index < 10; ++index)
    {
        QMetaObject::invokeMethod(&viewer, "zoom_in");
    }
    viewer.set_image_path("/missing/imageviewer.png");
    QTRY_COMPARE(viewer.windowTitle(), QString("加载图片失败"));
    auto* view = viewer.findChild<QGraphicsView*>();
    QCOMPARE(view->scene()->items().size(), 1);
    const QRectF visible = view->mapToScene(view->viewport()->rect()).boundingRect();
    QVERIFY(visible.contains(view->scene()->items().first()->sceneBoundingRect().center()));
}

void correctness_test::desktop_interactions()
{
    QTemporaryDir dir;
    QImage image(800, 600, QImage::Format_RGB32);
    image.fill(Qt::cyan);
    for (int index = 0; index < 80; ++index)
    {
        QVERIFY(image.save(dir.filePath(QString("image-%1.png").arg(index, 3, 10, QChar('0')))));
    }
    QFile gif(dir.filePath("animation.gif"));
    QVERIFY(gif.open(QIODevice::WriteOnly));
    gif.write(QByteArray::fromBase64(kAnimatedGif));
    gif.close();
    QByteArray bmp;
    QBuffer buffer(&bmp);
    QVERIFY(buffer.open(QIODevice::WriteOnly));
    QVERIFY(image.save(&buffer, "BMP"));
    QFile broken(dir.filePath("z-broken.bmp"));
    QVERIFY(broken.open(QIODevice::WriteOnly));
    broken.write(bmp.left(54));
    broken.close();
    main_window window;
    window.resize(800, 500);
    window.show();
    if (QGuiApplication::platformName() == "xcb")
    {
        QVERIFY(QTest::qWaitForWindowExposed(&window));
    }
    QMimeData mime;
    mime.setUrls({QUrl::fromLocalFile(dir.path())});
    QDragEnterEvent enter(QPoint(1, 1), Qt::CopyAction, &mime, Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(&window, &enter);
    QDropEvent drop(QPointF(1, 1), Qt::CopyAction, &mime, Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(&window, &drop);
    QVERIFY(drop.isAccepted());
    auto* scene = window.findChild<waterfall_scene*>();
    auto* view = window.findChild<waterfall_view*>();
    QVERIFY(scene != nullptr && view != nullptr);
    QTRY_COMPARE(scene->last_layout_index_, size_t(82));
    qInfo() << "Desktop" << QGuiApplication::platformName() << "DPR" << view->devicePixelRatioF();
    auto* scroll_bar = view->verticalScrollBar();
    QVERIFY(scroll_bar->maximum() > 0);
    const int scroll_middle = scroll_bar->maximum() / 2;
    scroll_bar->setValue(scroll_middle);
    view->setFocus();
    QTest::keyClick(view, Qt::Key_J);
    QVERIFY(scroll_bar->value() > scroll_middle);
    QTest::keyClick(view, Qt::Key_K);
    QCOMPARE(scroll_bar->value(), scroll_middle);
    QTest::keyClicks(view, "gg");
    QCOMPARE(scroll_bar->value(), 0);
    QTest::keyClick(view, Qt::Key_G, Qt::ShiftModifier);
    QCOMPARE(scroll_bar->value(), scroll_bar->maximum());
    const QString anchor_path = dir.filePath("image-040.png");
    QVERIFY(scene->focus_path(anchor_path));
    QTest::qWait(40);
    for (bool maximize : {true, false})
    {
        if (maximize) { window.showMaximized(); }
        else { window.showNormal(); }
        QTest::qWait(300);
        QTRY_COMPARE(qRound(scene->sceneRect().width()), view->viewport()->width());
        QTRY_VERIFY(!scene->is_laying_out_);
        const auto index = scene->image_paths_.indexOf(anchor_path);
        const QRectF visible = view->mapToScene(view->viewport()->rect()).boundingRect();
        QVERIFY2(std::abs(scene->all_models_[index].layout_rect.center().y() - visible.center().y()) < visible.height() / 4,
                 qPrintable(QString("maximized=%1 imageY=%2 viewY=%3 height=%4 width=%5").arg(maximize)
                    .arg(scene->all_models_[index].layout_rect.center().y()).arg(visible.center().y()).arg(visible.height()).arg(view->viewport()->width())));
    }
    const int index = scene->image_paths_.indexOf(anchor_path);
    QTRY_VERIFY(scene->active_items_.contains(index));
    QTRY_COMPARE(scene->active_items_[index]->state(), waterfall_item::load_state::ready);
    const QPoint point = view->mapFromScene(scene->all_models_[index].layout_rect.center());
    QVERIFY(view->viewport()->rect().contains(point));
    QTest::mouseClick(view->viewport(), Qt::LeftButton, Qt::NoModifier, point);
    QVERIFY(scene->active_items_[index]->isSelected());
    QTest::mouseDClick(view->viewport(), Qt::LeftButton, Qt::NoModifier, point);
    QTRY_VERIFY(window.findChild<image_viewer_window*>() != nullptr);
    auto* viewer = window.findChild<image_viewer_window*>();
    QVERIFY(viewer->isVisible());
    QTRY_VERIFY(viewer->windowTitle().startsWith("Viewer -"));
    QCOMPARE(viewer->current_image_path(), anchor_path);
    auto* preview = viewer->findChild<QGraphicsView*>();
    const qreal old_scale = preview->transform().m11();
    const QPointF center = preview->viewport()->rect().center();
    QWheelEvent wheel(center, preview->viewport()->mapToGlobal(center.toPoint()), QPoint(), QPoint(0, 120),
                      Qt::NoButton, Qt::ControlModifier, Qt::NoScrollPhase, false);
    QApplication::sendEvent(preview->viewport(), &wheel);
    QVERIFY(preview->transform().m11() > old_scale);
    const QRectF before = preview->scene()->items().first()->sceneBoundingRect();
    QVERIFY(QMetaObject::invokeMethod(viewer, "rotate_right"));
    const QRectF after = preview->scene()->items().first()->sceneBoundingRect();
    QCOMPARE(qRound(after.width()), qRound(before.height()));
    QTest::keyClick(preview, Qt::Key_Right);
    QTRY_COMPARE(viewer->current_image_path(), dir.filePath("image-041.png"));
    QTRY_VERIFY(viewer->windowTitle().contains("image-041.png"));
    viewer->set_image_path(gif.fileName());
    QTRY_VERIFY(viewer->findChild<QMovie*>() != nullptr);
    viewer->close();
    QTRY_VERIFY(viewer->findChild<QMovie*>() == nullptr);
    window.raise();
    window.activateWindow();
    QVERIFY(scene->focus_path(anchor_path));
    QTest::qWait(40);
    const QPoint reopen_point = view->mapFromScene(scene->all_models_[index].layout_rect.center());
    QVERIFY(view->viewport()->rect().contains(reopen_point));
    QTest::mouseClick(view->viewport(), Qt::LeftButton, Qt::NoModifier, reopen_point);
    QTest::mouseDClick(view->viewport(), Qt::LeftButton, Qt::NoModifier, reopen_point);
    QTRY_VERIFY(viewer->isVisible() && viewer->windowTitle().contains("image-040.png"));
    viewer->close();
    QVERIFY(scene->focus_path(broken.fileName()));
    const int failed_index = scene->image_paths_.indexOf(broken.fileName());
    QTRY_VERIFY(scene->active_items_.contains(failed_index));
    QTRY_COMPARE(scene->active_items_[failed_index]->state(), waterfall_item::load_state::failed);
    const QString capture = qEnvironmentVariable("IV_UI_CAPTURE");
    if (!capture.isEmpty())
    {
        window.raise();
        window.activateWindow();
        QTest::qWait(200);
        QVERIFY(window.screen()->grabWindow(window.winId()).save(capture));
    }
    window.close();
    QVERIFY(!viewer->isVisible());
}

void correctness_test::viewer_status_tracks_loading()
{
    QTemporaryDir dir;
    const QString path = dir.filePath("image.png");
    QImage image(100, 50, QImage::Format_RGB32);
    image.fill(Qt::blue);
    QVERIFY(image.save(path));
    image_viewer_window viewer;
    viewer.set_image_path(path);
    QTRY_VERIFY(viewer.windowTitle().startsWith("Viewer -"));
    const auto labels = viewer.findChildren<QLabel*>();
    QVERIFY(!labels.isEmpty());
    QVERIFY(std::any_of(labels.cbegin(), labels.cend(), [](QLabel* label)
    {
        return label->text().contains("image.png");
    }));
    viewer.set_image_path("/missing/imageviewer.png");
    for (auto* label : labels)
    {
        QVERIFY2(label->text().isEmpty(), qPrintable(label->text()));
    }
    QTRY_COMPARE(viewer.windowTitle(), QString("加载图片失败"));
    for (auto* label : labels)
    {
        QVERIFY(label->text().isEmpty());
    }
}

void correctness_test::scanner_sort_modes_data()
{
    QTest::addColumn<int>("mode");
    QTest::addColumn<bool>("descending");
    for (int mode = 0; mode < 3; ++mode)
    {
        QTest::newRow(qPrintable(QString("%1-up").arg(mode))) << mode << false;
        QTest::newRow(qPrintable(QString("%1-down").arg(mode))) << mode << true;
    }
}

void correctness_test::scanner_sort_modes()
{
    QFETCH(int, mode);
    QFETCH(bool, descending);
    QTemporaryDir dir;
    const QString large = dir.filePath("image2.png");
    const QString small = dir.filePath("image10.png");
    QImage image(100, 50, QImage::Format_RGB32);
    image.fill(Qt::red);
    QVERIFY(image.save(large));
    QVERIFY(image.scaled(2, 2).save(small));
    QFile file(small);
    QVERIFY(file.open(QIODevice::ReadWrite));
    QVERIFY(file.setFileTime(QFileInfo(large).lastModified().addSecs(-10), QFileDevice::FileModificationTime));
    file.close();
    file_scanner scanner;
    QSignalSpy batches(&scanner, &file_scanner::images_scanned_batch);
    scanner.start_scan(dir.path(), scanner.stop_scan(), mode, descending);
    QCOMPARE(batches.size(), 1);
    const auto images = qvariant_cast<QList<image_meta>>(batches.first().first());
    const bool large_first = mode == 0 ? !descending : descending;
    QCOMPARE(images.first().path, large_first ? large : small);
}

void correctness_test::clear_resets_scroll_range()
{
    waterfall_scene scene;
    QGraphicsView view(&scene);
    view.resize(440, 600);
    view.show();
    QList<image_meta> images;
    for (int index = 0; index < 100; ++index)
    {
        images.append({QString::number(index), QSize(100, 100)});
    }
    scene.add_images(images);
    scene.layout_models(440);
    QTRY_VERIFY(view.verticalScrollBar()->maximum() > 0);
    scene.clear_items();
    QTRY_COMPARE_WITH_TIMEOUT(view.verticalScrollBar()->maximum(), 0, 300);
}

void correctness_test::reprioritize_after_scroll()
{
    loader_worker worker;
    waterfall_scene scene;
    connect(&scene, &waterfall_scene::request_load_batch, worker.loader, &image_loader::request_thumbnails, Qt::DirectConnection);
    connect(&scene, &waterfall_scene::request_cancel_batch, worker.loader, &image_loader::cancel_thumbnails, Qt::DirectConnection);
    connect(&scene, &waterfall_scene::request_prioritize_batch, worker.loader, &image_loader::prioritize_thumbnails, Qt::DirectConnection);
    QList<image_meta> images;
    for (int index = 0; index < 100; ++index)
    {
        images.append({QString::number(index), QSize(100, 100)});
    }
    scene.add_images(images);
    scene.layout_models(440);
    QTRY_COMPARE(scene.sceneRect().width(), 440);
    scene.update_viewport(QRectF(0, 0, 440, 300));
    scene.update_viewport(QRectF(0, 500, 440, 300));
    QVERIFY(!worker.loader->task_queue_.isEmpty());
    const int first = worker.loader->task_queue_.first().path.toInt();
    QVERIFY2(first >= 4 && first <= 7, qPrintable(QString("first queued index: %1").arg(first)));
}

void correctness_test::same_path_replaced_inflight()
{
    QTemporaryDir dir;
    const QString path = dir.filePath("image.png");
    QImage image(100, 50, QImage::Format_RGB32);
    image.fill(Qt::red);
    QVERIFY(image.save(path));
    const QDateTime modified = QFileInfo(path).lastModified();
    image_viewer_window viewer;
    viewer.set_image_path(path);
    QVERIFY(QThreadPool::globalInstance()->waitForDone(5000));
    image.fill(Qt::blue);
    QVERIFY(image.save(path));
    QFile file(path);
    QVERIFY(file.open(QIODevice::ReadWrite));
    QVERIFY(file.setFileTime(modified.addSecs(2), QFileDevice::FileModificationTime));
    file.close();
    viewer.set_image_path(path);
    QTRY_VERIFY(viewer.windowTitle().startsWith("Viewer -"));
    auto* scene = viewer.findChild<QGraphicsView*>()->scene();
    auto* item = dynamic_cast<QGraphicsPixmapItem*>(scene->items().first());
    QVERIFY(item != nullptr);
    QCOMPARE(item->pixmap().toImage().pixelColor(0, 0), QColor(Qt::blue));
}

void correctness_test::layout_result_is_consumed()
{
    waterfall_scene scene;
    scene.add_images({{"first.png", QSize(100, 50)}});
    scene.layout_models(440);
    QTRY_COMPARE(scene.sceneRect().width(), 440);
    const auto future = scene.layout_watcher_.future();
    QVERIFY(!future.isValid() || future.result().rects.empty());
}

void correctness_test::natural_sort_in_c_locale()
{
    const QLocale previous;
    const auto restore = qScopeGuard([previous]() { QLocale::setDefault(previous); });
    QLocale::setDefault(QLocale::c());
    QTemporaryDir dir;
    QImage image(2, 2, QImage::Format_RGB32);
    image.fill(Qt::blue);
    QVERIFY(image.save(dir.filePath("image2.png")));
    QVERIFY(image.save(dir.filePath("image10.png")));
    file_scanner scanner;
    QSignalSpy batches(&scanner, &file_scanner::images_scanned_batch);
    scanner.start_scan(dir.path(), scanner.stop_scan(), 0, false);
    QCOMPARE(batches.size(), 1);
    const auto images = qvariant_cast<QList<image_meta>>(batches.first().first());
    QCOMPARE(QFileInfo(images.first().path).fileName(), QString("image2.png"));
}

void correctness_test::viewer_snapshot_is_shared()
{
    QStringList paths = {"/missing/first.png", "/missing/second.png"};
    QVERIFY(paths.isDetached());
    image_viewer_window viewer;
    viewer.set_image_list(paths);
    QVERIFY(!paths.isDetached());
    viewer.set_image_path(paths.at(0));
    QVERIFY(!paths.isDetached());
    QMetaObject::invokeMethod(&viewer, "load_next_image");
    QVERIFY(!paths.isDetached());
}

void correctness_test::thumbnail_native_size_data()
{
    QTest::addColumn<QByteArray>("format");
    for (const auto* format : {"jpg", "png", "webp", "bmp", "gif"})
    {
        QTest::newRow(format) << QByteArray(format);
    }
}

void correctness_test::thumbnail_native_size()
{
    QFETCH(QByteArray, format);
    if (!QImageReader::supportedImageFormats().contains(format))
    {
        QSKIP("当前 Qt 未安装此图片格式插件");
    }
    QTemporaryDir dir;
    const QString path = dir.filePath("small." + QString::fromLatin1(format));
    QSize expected(64, 32);
    if (format == "gif")
    {
        QFile file(path);
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write(QByteArray::fromBase64(kAnimatedGif));
        expected = QSize(2, 2);
    }
    else
    {
        QImage image(expected, QImage::Format_RGB32);
        image.fill(Qt::blue);
        QVERIFY(image.save(path));
    }
    loader_worker worker;
    QSignalSpy loaded(worker.loader, &image_loader::thumbnail_loaded);
    worker.thread.start();
    worker.loader->request_thumbnails({{1, path, QSize(256, 128), 1}});
    QTRY_COMPARE(loaded.size(), 1);
    QCOMPARE(qvariant_cast<QImage>(loaded.first()[2]).size(), expected);
}

void correctness_test::item_uses_smooth_transform()
{
    QGraphicsScene scene;
    scene.setSceneRect(0, 0, 200, 100);
    auto* item = new waterfall_item();
    scene.addItem(item);
    item->bind_model({QSize(2, 1), QRectF(0, 0, 200, 100)}, 0, "small.png", 200, 1);
    QImage source(2, 1, QImage::Format_RGB32);
    source.setPixelColor(0, 0, Qt::red);
    source.setPixelColor(1, 0, Qt::blue);
    item->set_pixmap_safe(QPixmap::fromImage(source));
    QImage output(200, 100, QImage::Format_RGB32);
    output.fill(Qt::white);
    QPainter painter(&output);
    scene.render(&painter);
    painter.end();
    const QColor center = output.pixelColor(99, 50);
    QVERIFY2(center.red() > 0 && center.blue() > 0, qPrintable(center.name()));
}

void correctness_test::trash_fresh_data_home()
{
#ifndef Q_OS_LINUX
    QSKIP("验证 Linux XDG 回收站首次创建");
#else
    QTemporaryDir root;
    const QString data_home = root.filePath("new-data");
    const QByteArray previous = qgetenv("XDG_DATA_HOME");
    qputenv("XDG_DATA_HOME", data_home.toUtf8());
    const auto restore = qScopeGuard([previous]()
    {
        if (previous.isNull()) { qunsetenv("XDG_DATA_HOME"); }
        else { qputenv("XDG_DATA_HOME", previous); }
    });
    const QString original = root.filePath("original.png");
    const QString copies = root.filePath("copies");
    QVERIFY(QDir().mkpath(copies));
    QImage image(80, 60, QImage::Format_RGB32);
    image.fill(Qt::blue);
    QVERIFY(image.save(original));
    const QString copy = copies + "/delete-copy.png";
    QVERIFY(QFile::copy(original, copy));
    QFile source(original);
    QVERIFY(source.open(QIODevice::ReadOnly));
    const QByteArray original_bytes = source.readAll();
    source.close();
    main_window window;
    window.show();
    auto* scene = window.findChild<waterfall_scene*>();
    QMimeData mime;
    mime.setUrls({QUrl::fromLocalFile(copies)});
    QDragEnterEvent enter(QPoint(1, 1), Qt::CopyAction, &mime, Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(&window, &enter);
    QDropEvent drop(QPointF(1, 1), Qt::CopyAction, &mime, Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(&window, &drop);
    QVERIFY(drop.isAccepted());
    QTRY_COMPARE(scene->get_all_paths().size(), 1);
    QVERIFY(!QFileInfo::exists(data_home));
    bool failed_dialog = false;
    QTimer confirm;
    confirm.setInterval(10);
    connect(&confirm, &QTimer::timeout, &window, [&failed_dialog]()
    {
        auto* box = qobject_cast<QMessageBox*>(QApplication::activeModalWidget());
        if (box == nullptr) { return; }
        if (box->windowTitle() == "移到回收站" && box->text().contains("delete-copy.png"))
        {
            box->button(QMessageBox::Yes)->click();
        }
        else if (box->windowTitle() == "移到回收站失败")
        {
            failed_dialog = true;
            box->button(QMessageBox::Ok)->click();
        }
    });
    confirm.start();
    scene->request_move_to_trash(copy);
    confirm.stop();
    QVERIFY(!failed_dialog);
    QVERIFY(!QFileInfo::exists(copy));
    const auto permissions = QFile::permissions(data_home);
    QVERIFY(permissions.testFlag(QFile::ReadOwner));
    QVERIFY(permissions.testFlag(QFile::WriteOwner));
    QVERIFY(permissions.testFlag(QFile::ExeOwner));
    QVERIFY(!(permissions & (QFile::ReadGroup | QFile::WriteGroup | QFile::ExeGroup |
                             QFile::ReadOther | QFile::WriteOther | QFile::ExeOther)));
    QTRY_VERIFY(scene->get_all_paths().isEmpty());
    const QStringList trashed = QDir(data_home + "/Trash/files").entryList(QDir::Files);
    QCOMPARE(trashed.size(), 1);
    QFile recovered(data_home + "/Trash/files/" + trashed.first());
    QVERIFY(recovered.open(QIODevice::ReadOnly));
    QCOMPARE(recovered.readAll(), original_bytes);
    QVERIFY(source.open(QIODevice::ReadOnly));
    QCOMPARE(source.readAll(), original_bytes);
#endif
}

void correctness_test::scanner_formats_data()
{
    QTest::addColumn<QByteArray>("format");
    for (const auto* format : {"jpg", "png", "bmp", "gif", "webp"})
    {
        QTest::newRow(format) << QByteArray(format);
    }
}

void correctness_test::scanner_formats()
{
    QFETCH(QByteArray, format);
    if (!QImageReader::supportedImageFormats().contains(format))
    {
        QSKIP("当前 Qt 未安装此图片格式插件");
    }
    QTemporaryDir dir;
    const QString path = dir.filePath("image." + QString::fromLatin1(format));
    QSize expected(12, 6);
    if (format == "gif")
    {
        QFile file(path);
        QVERIFY(file.open(QIODevice::WriteOnly));
        QVERIFY(file.write(QByteArray::fromBase64(kAnimatedGif)) > 0);
        expected = QSize(2, 2);
    }
    else
    {
        QImage image(expected, QImage::Format_RGB32);
        image.fill(Qt::blue);
        QVERIFY(image.save(path));
    }
    file_scanner scanner;
    QSignalSpy batches(&scanner, &file_scanner::images_scanned_batch);
    scanner.start_scan(dir.path(), scanner.stop_scan(), 0, false);
    QCOMPARE(batches.size(), 1);
    const auto images = qvariant_cast<QList<image_meta>>(batches.first().first());
    QCOMPARE(images.first().original_size, expected);
    image_viewer_window viewer;
    viewer.set_image_path(path);
    viewer.show();
    QTRY_VERIFY(viewer.windowTitle().startsWith("Viewer -"));
    auto* view = viewer.findChild<QGraphicsView*>();
    auto* item = qgraphicsitem_cast<QGraphicsPixmapItem*>(view->scene()->items().first());
    QVERIFY(item != nullptr);
    QCOMPARE(item->pixmap().size(), expected);
}

void correctness_test::jpeg_cache_roundtrip()
{
    QTemporaryDir dir;
    const QString path = dir.filePath("gradient.jpg");
    QImage image(200, 100, QImage::Format_RGB32);
    for (int y = 0; y < image.height(); ++y)
    {
        for (int x = 0; x < image.width(); ++x)
        {
            image.setPixelColor(x, y, QColor(x, y * 2, (x + y) / 2));
        }
    }
    QVERIFY(image.save(path, "JPEG", 95));
    const load_task task{1, path, image.size(), 1};
    QImage first;
    for (int pass = 0; pass < 2; ++pass)
    {
        loader_worker worker;
        QSignalSpy loaded(worker.loader, &image_loader::thumbnail_loaded);
        worker.loader->request_thumbnails({task});
        worker.thread.start();
        QTRY_COMPARE(loaded.size(), 1);
        const QImage decoded = qvariant_cast<QImage>(loaded.first()[2]);
        QCOMPARE(decoded.size(), image.size());
        QMetaObject::invokeMethod(worker.loader, []() {}, Qt::BlockingQueuedConnection);
        if (pass == 0)
        {
            first = decoded;
            const QByteArray format = QImageReader(worker.loader->disk_cache_path(task)).format();
            QVERIFY(format == "jpeg" || format == "jpg");
            continue;
        }
        qint64 error = 0;
        for (int y = 0; y < decoded.height(); ++y)
        {
            for (int x = 0; x < decoded.width(); ++x)
            {
                const QColor a = first.pixelColor(x, y);
                const QColor b = decoded.pixelColor(x, y);
                error += std::abs(a.red() - b.red()) + std::abs(a.green() - b.green()) + std::abs(a.blue() - b.blue());
            }
        }
        QVERIFY(error / (3.0 * decoded.width() * decoded.height()) < 2.0);
    }
}

void correctness_test::only_visible_requests()
{
    waterfall_scene scene;
    QList<image_meta> images;
    for (int index = 0; index < 100; ++index)
    {
        images.append({QString::number(index), QSize(100, 100)});
    }
    scene.add_images(images);
    scene.layout_models(440);
    QTRY_VERIFY(!scene.is_laying_out_);
    QSignalSpy requests(&scene, &waterfall_scene::request_load_batch);
    const QRectF visible(0, 500, 440, 300);
    scene.update_viewport(visible);
    QCOMPARE(requests.size(), 1);
    const auto tasks = qvariant_cast<QList<load_task>>(requests.first().first());
    QVERIFY(!tasks.isEmpty());
    for (const auto& task : tasks)
    {
        QVERIFY2(scene.all_models_[task.path.toInt()].layout_rect.intersects(visible), qPrintable(task.path));
    }
}

void correctness_test::scrolled_result_skips_old()
{
    waterfall_scene scene;
    QGraphicsView view(&scene);
    view.resize(440, 2000);
    view.show();
    QTRY_VERIFY(view.viewport()->height() > 1900);
    QList<image_meta> images;
    for (int index = 0; index < 1000; ++index)
    {
        images.append({QString::number(index), QSize(200, 2)});
    }
    QSignalSpy requests(&scene, &waterfall_scene::request_load_batch);
    scene.add_images(images);
    scene.layout_models(440);
    QTRY_VERIFY(!scene.is_laying_out_);
    view.verticalScrollBar()->setValue(0);
    scene.update_viewport(view.mapToScene(view.viewport()->rect()).boundingRect());
    QVERIFY(!requests.isEmpty());
    const auto task = qvariant_cast<QList<load_task>>(requests.last().first()).first();
    requests.clear();
    view.verticalScrollBar()->setValue(4000);
    scene.on_image_loaded(task.id, task.path, QImage(), 0);
    QVERIFY(requests.isEmpty());
    scene.update_viewport(view.mapToScene(view.viewport()->rect()).boundingRect());
    QVERIFY(!requests.isEmpty());
}

void correctness_test::viewer_keeps_window_geometry()
{
    QTemporaryDir dir;
    const QString wide = dir.filePath("wide.png");
    const QString tall = dir.filePath("tall.png");
    const QString animated = dir.filePath("animated.gif");
    QImage image(1200, 600, QImage::Format_RGB32);
    image.fill(Qt::blue);
    QVERIFY(image.save(wide));
    QVERIFY(image.scaled(120, 300).save(tall));
    QFile gif(animated);
    QVERIFY(gif.open(QIODevice::WriteOnly));
    QVERIFY(gif.write(QByteArray::fromBase64(kAnimatedGif)) > 0);
    gif.close();

    image_viewer_window viewer;
    viewer.resize(640, 440);
    viewer.show();
    QTest::qWait(50);
    const QRect geometry = viewer.geometry();
    viewer.set_image_list({wide, tall, animated});
    for (const QString& path : {wide, tall, animated})
    {
        viewer.set_image_path(path);
        QTRY_VERIFY(viewer.windowTitle().startsWith("Viewer -"));
        QCOMPARE(viewer.geometry(), geometry);
    }
    viewer.close();
}

void correctness_test::viewer_rotation_shortcut()
{
    QTemporaryDir dir;
    QImage image(100, 50, QImage::Format_RGB32);
    image.fill(Qt::blue);
    QVERIFY(image.save(dir.filePath("image.png")));
    main_window window;
    window.show();
    image_viewer_window viewer(&window);
    viewer.set_image_path(dir.filePath("image.png"));
    viewer.show();
    viewer.activateWindow();
    QTRY_VERIFY(viewer.windowTitle().startsWith("Viewer -"));
    auto* preview = viewer.findChild<QGraphicsView*>();
    preview->setFocus();
    QTest::qWait(50);
    QTest::keyClick(preview, Qt::Key_L, Qt::ControlModifier);
    QTRY_COMPARE(qRound(preview->scene()->sceneRect().width()), 50);
}

void correctness_test::open_selected_with_keyboard_data()
{
    QTest::addColumn<int>("key");
    QTest::newRow("return") << int(Qt::Key_Return);
    QTest::newRow("enter") << int(Qt::Key_Enter);
    QTest::newRow("space") << int(Qt::Key_Space);
}

void correctness_test::open_selected_with_keyboard()
{
    QFETCH(int, key);
    QTemporaryDir dir;
    const QString path = dir.filePath("image.png");
    QImage image(100, 50, QImage::Format_RGB32);
    image.fill(Qt::blue);
    QVERIFY(image.save(path));
    main_window window;
    window.show();
    window.activateWindow();
    QVERIFY(drop_folder(window, dir.path()));
    auto* scene = window.findChild<waterfall_scene*>();
    auto* view = window.findChild<waterfall_view*>();
    QTRY_COMPARE(scene->get_all_paths().size(), 1);
    QTRY_VERIFY(scene->focus_path(path));
    view->setFocus();
    QTest::qWait(50);
    QTest::keyClick(view, static_cast<Qt::Key>(key));
    QTRY_VERIFY(window.findChild<image_viewer_window*>() != nullptr);
    auto* viewer = window.findChild<image_viewer_window*>();
    QVERIFY(viewer->isVisible());
    QVERIFY(viewer->windowFlags().testFlag(Qt::FramelessWindowHint));
    QCOMPARE(viewer->current_image_path(), path);
}

void correctness_test::viewer_wheel_zoom_data()
{
    QTest::addColumn<QPoint>("angle");
    QTest::addColumn<QPoint>("pixels");
    QTest::addColumn<int>("phase");
    QTest::addColumn<bool>("control");
    QTest::addColumn<double>("factor");
    QTest::newRow("angle-up") << QPoint(0, 120) << QPoint() << int(Qt::NoScrollPhase) << true << 1.2;
    QTest::newRow("angle-down") << QPoint(0, -120) << QPoint() << int(Qt::NoScrollPhase) << true << 1.0 / 1.2;
    QTest::newRow("horizontal-left") << QPoint(-120, 0) << QPoint() << int(Qt::NoScrollPhase) << true << 1.0;
    QTest::newRow("horizontal-right") << QPoint(120, 0) << QPoint() << int(Qt::NoScrollPhase) << true << 1.0;
    QTest::newRow("zero-begin") << QPoint() << QPoint() << int(Qt::ScrollBegin) << true << 1.0;
    QTest::newRow("zero-end") << QPoint() << QPoint() << int(Qt::ScrollEnd) << true << 1.0;
    QTest::newRow("zero-update") << QPoint() << QPoint() << int(Qt::ScrollUpdate) << true << 1.0;
    QTest::newRow("pixel-up") << QPoint() << QPoint(0, 12) << int(Qt::ScrollUpdate) << true << 1.2;
    QTest::newRow("pixel-down") << QPoint() << QPoint(0, -12) << int(Qt::ScrollUpdate) << true << 1.0 / 1.2;
    QTest::newRow("pixel-horizontal") << QPoint() << QPoint(12, 0) << int(Qt::ScrollUpdate) << true << 1.0;
    QTest::newRow("pan-angle") << QPoint(0, 120) << QPoint() << int(Qt::NoScrollPhase) << false << 1.0;
    QTest::newRow("pan-pixel") << QPoint() << QPoint(0, 12) << int(Qt::ScrollUpdate) << false << 1.0;
    QTest::newRow("angle-priority") << QPoint(0, 120) << QPoint(0, -12) << int(Qt::ScrollUpdate) << true << 1.2;
    QTest::newRow("diagonal") << QPoint(120, 120) << QPoint() << int(Qt::ScrollUpdate) << true << 1.2;
}

void correctness_test::viewer_wheel_zoom()
{
    QFETCH(QPoint, angle);
    QFETCH(QPoint, pixels);
    QFETCH(int, phase);
    QFETCH(bool, control);
    QFETCH(double, factor);
    QTemporaryDir dir;
    QImage image(80, 60, QImage::Format_RGB32);
    image.fill(Qt::cyan);
    const QString path = dir.filePath("image.png");
    QVERIFY(image.save(path));
    image_viewer_window viewer;
    viewer.resize(700, 520);
    viewer.show();
    viewer.set_image_path(path);
    QTRY_VERIFY(viewer.windowTitle().startsWith("Viewer -"));
    auto* view = viewer.findChild<QGraphicsView*>();
    const qreal before = view->transform().m11();
    const QPointF center = view->viewport()->rect().center();
    QWheelEvent wheel(center, view->viewport()->mapToGlobal(center.toPoint()), pixels, angle,
                      Qt::NoButton, control ? Qt::ControlModifier : Qt::NoModifier,
                      static_cast<Qt::ScrollPhase>(phase), false);
    QApplication::sendEvent(view->viewport(), &wheel);
    QCOMPARE(view->transform().m11() / before, factor);
    QCOMPARE(viewer.current_image_path(), path);
}

void correctness_test::viewer_mouse_navigation()
{
    image_viewer_window viewer;
    const QStringList paths = {"/missing/first.png", "/missing/middle.png", "/missing/last.png"};
    viewer.set_image_list(paths);
    viewer.set_image_path(paths[1]);
    viewer.show();
    auto* viewport = viewer.findChild<QGraphicsView*>()->viewport();
    QTest::mouseClick(viewport, Qt::BackButton);
    QCOMPARE(viewer.current_image_path(), paths[0]);
    QTest::mouseClick(viewport, Qt::BackButton);
    QCOMPARE(viewer.current_image_path(), paths[0]);
    QTest::mouseClick(viewport, Qt::ForwardButton);
    QCOMPARE(viewer.current_image_path(), paths[1]);
    auto* view = viewer.findChild<QGraphicsView*>();
    QTest::keyClick(view, Qt::Key_H);
    QCOMPARE(viewer.current_image_path(), paths[0]);
    QTest::keyClick(view, Qt::Key_L);
    QCOMPARE(viewer.current_image_path(), paths[1]);
}

void correctness_test::compact_sort_controls()
{
    QTemporaryDir dir;
    const QString small = dir.filePath("z-small.png");
    const QString large = dir.filePath("a-large.png");
    QImage image(800, 600, QImage::Format_RGB32);
    image.fill(Qt::blue);
    QVERIFY(image.save(large));
    QVERIFY(image.scaled(80, 60).save(small));
    QVERIFY(QFileInfo(small).size() < QFileInfo(large).size());
    main_window window;
    window.resize(640, 480);
    window.show();
    auto* button = window.findChild<QToolButton*>("sort_options");
    QVERIFY(button != nullptr && button->menu() != nullptr);
    QTest::qWait(50);
    QVERIFY(button->isVisibleTo(&window));
    QVERIFY(window.rect().contains(button->mapTo(&window, button->rect().bottomRight())));
    QVERIFY(drop_folder(window, dir.path()));
    auto* scene = window.findChild<waterfall_scene*>();
    QTRY_COMPARE(scene->get_all_paths().size(), 2);
    QAction* size_action = nullptr;
    QAction* reverse_action = nullptr;
    QMenu* sort = nullptr;
    sort = button->menu();
    for (auto* action : sort->actions())
    {
        if (action->text() == "文件大小") { size_action = action; }
        if (action->text() == "倒序") { reverse_action = action; }
    }
    QVERIFY(size_action != nullptr && reverse_action != nullptr);
    if (reverse_action->isChecked()) { reverse_action->trigger(); }
    size_action->trigger();
    QTRY_COMPARE(scene->get_all_paths().value(0), small);
    QVERIFY(size_action->isChecked());
    reverse_action->trigger();
    QTRY_COMPARE(scene->get_all_paths().value(0), large);
}

void correctness_test::compact_preview_controls()
{
    image_viewer_window viewer;
    QStringList paths;
    for (int index = 0; index < 10000; ++index)
    {
        paths.append(QString("/missing/%1.png").arg(index));
    }
    viewer.set_image_list(paths);
    viewer.set_image_path(paths[5000]);
    viewer.resize(640, 440);
    viewer.show();
    QTest::qWait(50);
    QToolButton* back = nullptr;
    QToolButton* more = nullptr;
    for (auto* button : viewer.findChildren<QToolButton*>())
    {
        if (button->text() == "图库") { back = button; }
        if (button->text() == "更多") { more = button; }
    }
    QVERIFY(back != nullptr && more != nullptr);
    auto* toolbar = viewer.findChild<QToolBar*>("preview_toolbar");
    QVERIFY(toolbar != nullptr);
    qInfo() << "Preview toolbar" << toolbar->sizeHint() << "window" << viewer.size()
            << "DPR" << viewer.devicePixelRatioF();
    QVERIFY(more->isVisibleTo(&viewer));
    QVERIFY(viewer.rect().contains(more->mapTo(&viewer, more->rect().bottomRight())));
    QTest::mouseClick(back, Qt::LeftButton);
    QVERIFY(!viewer.isVisible());
}

void correctness_test::custom_header_data()
{
    QTest::addColumn<bool>("preview");
    QTest::newRow("library") << false;
    QTest::newRow("preview") << true;
}

void correctness_test::custom_header()
{
    QFETCH(bool, preview);
    std::unique_ptr<QMainWindow> window;
    if (preview) { window = std::make_unique<image_viewer_window>(); }
    else { window = std::make_unique<main_window>(); }
    window->resize(640, 440);
    window->show();
    window->activateWindow();
    QTest::qWait(50);
    QVERIFY(window->windowFlags().testFlag(Qt::FramelessWindowHint));
    QVERIFY(window->findChildren<QLineEdit*>().isEmpty());
    auto* header = window->findChild<QWidget*>("window_title_bar");
    QVERIFY(header != nullptr);
    for (auto* button : header->findChildren<QToolButton*>())
    {
        if (button->objectName() == "qt_toolbar_ext_button") { continue; }
        if (preview || button->objectName().startsWith("window_"))
        {
            QCOMPARE(button->toolButtonStyle(), Qt::ToolButtonIconOnly);
            QVERIFY(!button->icon().isNull());
            QVERIFY(!button->toolTip().isEmpty());
        }
        else
        {
            QCOMPARE(button->toolButtonStyle(), Qt::ToolButtonTextOnly);
            QVERIFY2(button->icon().isNull(), qPrintable(button->objectName() + ": icon remains"));
            QVERIFY2(button->toolTip().isEmpty(), qPrintable(button->objectName() + ": tooltip remains: " + button->toolTip()));
            QVERIFY(!button->accessibleName().isEmpty());
        }
    }
    auto* maximize = header->findChild<QToolButton*>("window_maximize");
    auto* minimize = header->findChild<QToolButton*>("window_minimize");
    auto* close = header->findChild<QToolButton*>("window_close");
    QVERIFY(maximize != nullptr && minimize != nullptr && close != nullptr);
    QTest::mouseClick(maximize, Qt::LeftButton);
    QTRY_VERIFY(window->isMaximized());
    QCOMPARE(maximize->toolTip(), QString("还原"));
    QTest::mouseDClick(header, Qt::LeftButton, Qt::NoModifier, QPoint(4, 4));
    QTRY_VERIFY(!window->isMaximized());
    QTest::mouseClick(minimize, Qt::LeftButton);
    QTRY_VERIFY(window->isMinimized());
    window->showNormal();
    QTest::qWait(50);
    QTest::mouseClick(close, Qt::LeftButton);
    QVERIFY(!window->isVisible());
}

void correctness_test::header_follows_theme()
{
    main_window library;
    image_viewer_window preview(&library);
    library.resize(640, 440);
    preview.resize(640, 440);
    library.show();
    preview.show();
    auto* theme = library.findChild<QAction*>("dark_theme");
    QVERIFY(theme != nullptr);
    for (bool dark : {false, true, false})
    {
        theme->setChecked(dark);
        QCoreApplication::processEvents();
        for (QMainWindow* window : {static_cast<QMainWindow*>(&library), static_cast<QMainWindow*>(&preview)})
        {
            auto* header = window->findChild<QWidget*>("window_title_bar");
            QVERIFY(header != nullptr);
            const QImage image = header->grab().toImage();
            QCOMPARE(image.pixelColor(image.width() / 2, qRound(2 * header->devicePixelRatioF())).rgb(),
                     QApplication::palette().color(QPalette::Window).rgb());
        }
    }
}

void correctness_test::popup_indicators_data()
{
    QTest::addColumn<bool>("preview");
    QTest::addColumn<QString>("text");
    QTest::newRow("library-sort") << false << QString("sort_options");
    QTest::newRow("preview-more") << true << QString("更多");
    QTest::newRow("preview-mode") << true << QString("适应窗口");
}

void correctness_test::popup_indicators()
{
    QFETCH(bool, preview);
    QFETCH(QString, text);
    std::unique_ptr<QMainWindow> window;
    if (preview) { window = std::make_unique<image_viewer_window>(); }
    else { window = std::make_unique<main_window>(); }
    window->resize(640, 440);
    window->show();
    QTest::qWait(30);
    QToolButton* button = nullptr;
    for (auto* candidate : window->findChildren<QToolButton*>())
    {
        if ((!preview && text == "sort_options" && candidate->objectName() == text)
            || candidate->text() == text)
        {
            button = candidate;
        }
    }
    QVERIFY(button != nullptr && button->menu() != nullptr);
    button->clearFocus();
    button->setFixedSize(button->size());
    const auto with_menu = button->grab().toImage();
    QMenu* menu = button->menu();
    button->setMenu(nullptr);
    const auto without_menu = button->grab().toImage();
    button->setMenu(menu);
    const qreal dpr = button->devicePixelRatioF();
    const QRect bottom(0, with_menu.height() - qRound(6 * dpr), with_menu.width(), qRound(5 * dpr));
    QCOMPARE(with_menu.copy(bottom), without_menu.copy(bottom));
    if (button->popupMode() == QToolButton::MenuButtonPopup)
    {
        const QRect right(with_menu.width() - qRound(12 * dpr), qRound(2 * dpr),
                          qRound(10 * dpr), with_menu.height() - qRound(4 * dpr));
        QCOMPARE(with_menu.copy(right), without_menu.copy(right));
    }
    bool opened = false;
    QTimer::singleShot(0, menu, [&]() { opened = menu->isVisible(); menu->close(); });
    const QPoint click = button->popupMode() == QToolButton::MenuButtonPopup ?
        QPoint(button->width() - 5, button->height() / 2) : button->rect().center();
    QTest::mouseClick(button, Qt::LeftButton, Qt::NoModifier, click);
    QVERIFY(opened);
}

void correctness_test::directory_context()
{
    QTemporaryDir dir;
    const QString folder = dir.filePath("城市与春天");
    QVERIFY(QDir().mkdir(folder));
    main_window window;
    window.resize(640, 440);
    window.show();
    QVERIFY(drop_folder(window, folder));
    QTRY_VERIFY([&]()
    {
        for (auto* label : window.statusBar()->findChildren<QLabel*>())
        {
            if (label->text().contains("城市与春天") && label->text().contains("没有")) { return true; }
        }
        return false;
    }());
    QVERIFY(window.findChildren<QLineEdit*>().isEmpty());
    QCOMPARE(window.width(), 640);
    const QString long_folder = dir.filePath("长目录" + QString(40, QChar(0x56fe)));
    QVERIFY(QDir().mkdir(long_folder));
    const QString name = QString(60, QChar(0x56fe)) + ".png";
    const QString path = QDir(long_folder).filePath(name);
    QImage image(80, 60, QImage::Format_RGB32);
    image.fill(Qt::blue);
    QVERIFY(image.save(path));
    QVERIFY(drop_folder(window, long_folder));
    auto* scene = window.findChild<waterfall_scene*>();
    QTRY_COMPARE(scene->last_layout_index_, size_t(1));
    QVERIFY(scene->focus_path(path));
    QTest::qWait(50);
    QCOMPARE(window.width(), 640);
}

void correctness_test::toolbar_uses_text_buttons()
{
    main_window window;
    auto* toolbar = window.findChild<QToolBar*>("browse_toolbar");
    auto* theme = window.findChild<QAction*>("dark_theme");
    QVERIFY(toolbar != nullptr && theme != nullptr);
    QVERIFY(window.findChild<QToolButton*>("more_menu") == nullptr);
    QVERIFY(window.findChild<QToolButton*>("sort_options") != nullptr);
    QCOMPARE(toolbar->toolButtonStyle(), Qt::ToolButtonTextOnly);
    for (bool dark : {false, true, false})
    {
        theme->setChecked(dark);
        QCOMPARE(theme->text(), dark ? QString("浅色主题") : QString("深色主题"));
        for (auto* button : toolbar->findChildren<QToolButton*>())
        {
            if (button->objectName() == "qt_toolbar_ext_button") { continue; }
            QCOMPARE(button->toolButtonStyle(), Qt::ToolButtonTextOnly);
            QVERIFY2(button->icon().isNull(), qPrintable(button->objectName() + ": icon remains"));
            QVERIFY2(button->toolTip().isEmpty(), qPrintable(button->objectName() + ": tooltip remains: " + button->toolTip()));
        }
    }
}

void correctness_test::card_focus_state()
{
    QGraphicsScene scene;
    scene.setSceneRect(0, 0, 400, 1200);
    auto* item = new waterfall_item();
    scene.addItem(item);
    layout_model model{QSize(200, 150), QRectF(0, 0, 200, 150)};
    item->bind_model(model, 0, "first.png", 200, 1);
    QVERIFY(item->flags().testFlag(QGraphicsItem::ItemIsFocusable));
    QGraphicsView view(&scene);
    view.resize(400, 300);
    view.show();
    view.activateWindow();
    QVERIFY(QTest::qWaitForWindowActive(&view));
    view.setFocus();
    scene.setFocus();
    item->setSelected(true);
    item->setFocus();
    QTRY_VERIFY(item->hasFocus());
    view.verticalScrollBar()->setValue(0);
    QTest::keyClick(&view, Qt::Key_Down);
    QVERIFY(view.verticalScrollBar()->value() > 0);
    QTest::keyClick(&view, Qt::Key_Up);
    QCOMPARE(view.verticalScrollBar()->value(), 0);
    const QImage focused = view.viewport()->grab().toImage();
    item->clearFocus();
    const QImage selected = view.viewport()->grab().toImage();
    QVERIFY(focused != selected);
    item->setFocus();
    item->reset();
    item->bind_model(model, 1, "second.png", 200, 2);
    QVERIFY(!item->hasFocus());
}

void correctness_test::scene_focus_tracks_path()
{
    waterfall_scene scene;
    waterfall_view view;
    view.setScene(&scene);
    view.resize(440, 400);
    view.show();
    scene.add_images({{"first.png", QSize(200, 150)}, {"second.png", QSize(200, 150)}});
    scene.layout_models(440);
    QTRY_COMPARE(scene.last_layout_index_, size_t(2));
    QVERIFY(scene.focus_path("first.png"));
    scene.active_items_[0]->setFocus();
    QCOMPARE(scene.focusItem(), static_cast<QGraphicsItem*>(scene.active_items_[0]));
    QVERIFY(scene.focus_path("second.png"));
    QCOMPARE(scene.focusItem(), static_cast<QGraphicsItem*>(scene.active_items_[1]));
}

void correctness_test::recent_menu_is_flat()
{
    QTemporaryDir dir;
    const QString image_path = dir.filePath("image.png");
    QImage image(100, 50, QImage::Format_RGB32);
    image.fill(Qt::blue);
    QVERIFY(image.save(image_path));
    QSettings settings("gyl30", "ImageViewer");
    settings.setValue("main_window/recent_folder_paths", QStringList{dir.path()});
    settings.setValue("main_window/recent_image_paths", QStringList{image_path});
    main_window window;
    window.show();
    QAction* recent = nullptr;
    for (auto* action : window.actions())
    {
        if (action->text() == "最近") { recent = action; }
    }
    QVERIFY(recent != nullptr);
    QStringList labels;
    QStringList paths;
    bool nested = false;
    QTimer::singleShot(0, &window, [&]()
    {
        auto* menu = qobject_cast<QMenu*>(QApplication::activePopupWidget());
        if (menu == nullptr) { return; }
        for (auto* action : menu->actions())
        {
            labels.append(action->text());
            paths.append(action->data().toString());
            nested = nested || action->menu() != nullptr;
        }
        menu->close();
    });
    recent->trigger();
    QVERIFY(!nested);
    QCOMPARE(labels.size(), 2);
    QVERIFY(labels[0].contains(QFileInfo(dir.path()).fileName()));
    QVERIFY(labels[1].contains("image.png"));
    QVERIFY(!labels.join(' ').contains(dir.path()));
    QCOMPARE(paths, QStringList({dir.path(), image_path}));
}

void correctness_test::sorting_is_in_toolbar()
{
    main_window window;
    QVERIFY(window.findChild<QToolButton*>("more_menu") == nullptr);
    auto* sort_button = window.findChild<QToolButton*>("sort_options");
    QVERIFY(sort_button != nullptr && sort_button->menu() != nullptr);
    QStringList names;
    for (auto* action : sort_button->menu()->actions()) { names.append(action->text()); }
    QVERIFY(names.contains("文件名") && names.contains("修改时间") && names.contains("文件大小"));
}

void correctness_test::xcb_header_gestures()
{
    constexpr int kWindowOperationTimeoutMs = 15000;
    if (QGuiApplication::platformName() != "xcb" || QGuiApplication::screens().size() != 1)
    {
        QSKIP("系统拖动和缩放仅在单屏X11上验证");
    }
    QLibrary x11("libX11.so.6");
    QLibrary xtst("libXtst.so.6");
    if (!x11.load() || !xtst.load()) { QSKIP("需要系统X11/XTest测试库"); }
    const auto open = reinterpret_cast<void* (*)(const char*)>(x11.resolve("XOpenDisplay"));
    const auto close = reinterpret_cast<int (*)(void*)>(x11.resolve("XCloseDisplay"));
    const auto flush = reinterpret_cast<int (*)(void*)>(x11.resolve("XFlush"));
    const auto screen = reinterpret_cast<int (*)(void*)>(x11.resolve("XDefaultScreen"));
    const auto motion = reinterpret_cast<int (*)(void*, int, int, int, unsigned long)>(xtst.resolve("XTestFakeMotionEvent"));
    const auto button = reinterpret_cast<int (*)(void*, unsigned int, int, unsigned long)>(xtst.resolve("XTestFakeButtonEvent"));
    QVERIFY(open != nullptr && close != nullptr && flush != nullptr && screen != nullptr &&
            motion != nullptr && button != nullptr);
    void* display = open(nullptr);
    QVERIFY(display != nullptr);
    const QPoint saved_pointer = QCursor::pos();
    const auto cleanup = qScopeGuard([&]()
    {
        button(display, 1, false, 0);
        flush(display);
        close(display);
        QCursor::setPos(saved_pointer);
    });
    main_window window;
    window.setWindowState(Qt::WindowNoState);
    window.setGeometry(120, 80, 640, 360);
    window.show();
    window.raise();
    window.activateWindow();
    QVERIFY(QTest::qWaitForWindowExposed(&window));
    QVERIFY(QTest::qWaitForWindowActive(&window, kWindowOperationTimeoutMs));
    const qreal dpr = window.devicePixelRatioF();
    const QPoint delta(60, 40);
    const auto drag = [&](const QPoint& from)
    {
        const QRect initial = window.geometry();
        motion(display, screen(display), qRound(from.x() * dpr), qRound(from.y() * dpr), 0);
        flush(display);
        QTest::qWait(50);
        button(display, 1, true, 0);
        flush(display);
        QTest::qWait(100);
        for (int step = 1; step <= 10; ++step)
        {
            const QPoint point = from + delta * step / 10;
            motion(display, screen(display), qRound(point.x() * dpr), qRound(point.y() * dpr), 0);
            flush(display);
            QTest::qWait(50);
        }
        const bool responded = QTest::qWaitFor([&]() { return window.geometry() != initial; }, kWindowOperationTimeoutMs);
        button(display, 1, false, 0);
        flush(display);
        return responded;
    };
    auto* header = window.findChild<QWidget*>("window_title_bar");
    QVERIFY(header != nullptr);
    const QPoint before = window.pos();
    QVERIFY(drag(header->mapToGlobal(header->rect().center())));
    QTRY_VERIFY_WITH_TIMEOUT(window.x() > before.x() && window.y() > before.y(), kWindowOperationTimeoutMs);
    // 系统异步接管时可能少消费开头的位移；验证方向和逻辑像素上界。
    QVERIFY(window.x() - before.x() <= delta.x() + 3);
    QVERIFY(window.y() - before.y() <= delta.y() + 3);
    const QSize size = window.size();
    const QPoint corner = window.mapToGlobal(QPoint(window.width() - 2, window.height() - 2));
    auto* hit = QApplication::widgetAt(corner);
    qInfo() << "Resize target" << hit << "geometry" << window.geometry() << "DPR" << dpr;
    QVERIFY(drag(corner));
    qInfo() << "Resize result" << window.geometry() << "size delta" << window.size() - size;
    QTRY_VERIFY_WITH_TIMEOUT(window.width() > size.width() && window.height() > size.height(), kWindowOperationTimeoutMs);
    QVERIFY(window.width() - size.width() <= delta.x() + 3);
    QVERIFY(window.height() - size.height() <= delta.y() + 3);
    QCursor::setPos(window.findChild<waterfall_view*>()->viewport()->mapToGlobal(QPoint(40, 40)));
    QTest::qWait(50);
    QCOMPARE(window.cursor().shape(), Qt::ArrowCursor);
}

int main(int argc, char** argv)
{
    QTemporaryDir settings_dir;
    qputenv("XDG_CONFIG_HOME", settings_dir.path().toUtf8());
    qputenv("XDG_CACHE_HOME", settings_dir.path().toUtf8());
    QImageReader::setAllocationLimit(kQtImageReaderAllocMB);
    QLocale::setDefault(QLocale(QLocale::English, QLocale::UnitedStates));
    QApplication app(argc, argv);
    app.setQuitOnLastWindowClosed(false);
    correctness_test test;
    return QTest::qExec(&test, argc, argv);
}

#include "correctness_test.moc"
