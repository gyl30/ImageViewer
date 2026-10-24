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

namespace
{
constexpr char kAnimatedGif[] =
    "R0lGODlhAgACAIEAAP8AAAAAAAAAAAAAACH/C05FVFNDQVBFMi4wAwEAAAAh+QQACgAAACwAAAAAAgACAAAIBgABCAQQEAAh+QQBCgABACwAAAAAAgACAIEAAP8AAAAAAAAAAAAIBgABCAQQEAA7";
}

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
    void scroll_updates_during_motion();
    void viewer_error_is_visible();
    void scanner_sort_modes_data();
    void scanner_sort_modes();
    void clear_resets_scroll_range();
    void reprioritize_after_scroll();
    void same_path_replaced_inflight();
    void layout_result_is_consumed();
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
        QVERIFY(image.save(directory + QString("/seed-%1.png").arg(i)));
    }
    image_loader loader;
    QVERIFY(QDir(directory).entryList({"*.png"}, QDir::Files).size() <= 1000);
    loader.clear_cache();
    QVERIFY(QDir(directory).entryList({"*.png"}, QDir::Files).isEmpty());
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
    QTemporaryDir dir;
    QVERIFY(QFile::setPermissions(dir.path(), {}));
    scanner.start_scan(dir.path(), scanner.stop_scan(), 0, false);
    QVERIFY(QFile::setPermissions(dir.path(), QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner));
    QCOMPARE(failed.size(), 2);
}

void correctness_test::theme_keeps_scene_items()
{
    main_window window;
    auto* theme = window.findChild<QAction*>("dark_theme");
    QVERIFY(theme != nullptr);
    QVERIFY(!theme->icon().pixmap(18, 18).isNull());
    auto* scene = window.findChild<waterfall_scene*>();
    const int items = scene->items().size();
    theme->setChecked(true);
    QVERIFY(QApplication::palette().base().color().lightness() < 128);
    QCOMPARE(scene->items().size(), items);
    theme->setChecked(false);
    QVERIFY(QApplication::palette().base().color().lightness() > 128);
    QCOMPARE(scene->items().size(), items);
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
    viewer.set_image_path("/missing/imageviewer.png");
    QTRY_COMPARE(viewer.windowTitle(), QString("加载图片失败"));
    auto* view = viewer.findChild<QGraphicsView*>();
    QCOMPARE(view->scene()->items().size(), 1);
    QVERIFY(view->mapToScene(view->viewport()->rect()).boundingRect().intersects(view->scene()->items().first()->sceneBoundingRect()));
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
