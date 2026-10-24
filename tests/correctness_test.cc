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
#include "waterfall_item.h"
#include "waterfall_scene.h"

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
    void preloads_after_reopen();
    void stale_queued_scans();
    void exif_orientation_data();
    void exif_orientation();
    void thumbnail_file_replaced();
    void viewer_file_replaced_data();
    void viewer_file_replaced();
    void movie_stops_on_close();
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
    view.resize(440, 600);
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
    QCOMPARE(item->scale() * item->pixmap().width(), 305);
}

void correctness_test::item_reuse_resets_image()
{
    waterfall_item item;
    layout_model model{0, "first.png", QSize(100, 50), QRectF(10, 10, 200, 100)};
    item.bind_model(model, 200, 1);
    QPixmap image(100, 50);
    image.fill(Qt::red);
    item.set_pixmap_safe(image);
    item.reset();
    model.path = "second.png";
    item.bind_model(model, 200, 2);
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

void correctness_test::preloads_after_reopen()
{
    QTemporaryDir dir;
    std::vector<QString> paths;
    for (int i = 0; i < 6; ++i)
    {
        QImage image(100, 50, QImage::Format_RGB32);
        image.fill(i == 3 ? Qt::blue : Qt::green);
        paths.push_back(dir.filePath(QString::number(i) + ".png"));
        QVERIFY(image.save(paths.back()));
    }

    image_viewer_window viewer;
    viewer.set_image_list(paths);
    viewer.show();
    viewer.set_image_path(paths[1]);
    QTRY_VERIFY(viewer.windowTitle().startsWith("Viewer -"));
    QVERIFY(viewer.close());
    viewer.show();
    viewer.set_image_path(paths[4]);
    QTRY_VERIFY(viewer.windowTitle().startsWith("Viewer -"));

    const auto both_neighbors_loaded = [&viewer]()
    {
        for (auto* watcher : viewer.findChildren<QFutureWatcherBase*>())
        {
            auto* preload = dynamic_cast<QFutureWatcher<std::pair<QString, QImage>>*>(watcher);
            if (preload != nullptr && preload->isFinished())
            {
                const QImage result = preload->result().second;
                return !result.isNull() && result.pixelColor(0, 0) == QColor(Qt::blue);
            }
        }
        return false;
    };
    QTRY_VERIFY_WITH_TIMEOUT(both_neighbors_loaded(), 500);
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
    QTest::addColumn<bool>("preloaded");
    QTest::newRow("foreground-cache") << false;
    QTest::newRow("preload-cache") << true;
}

void correctness_test::viewer_file_replaced()
{
    QFETCH(bool, preloaded);
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
    viewer.set_image_path(preloaded ? neighbor : path);
    QTRY_VERIFY(viewer.windowTitle().startsWith("Viewer -"));
    if (preloaded)
    {
        const auto cached_neighbor = [&viewer]()
        {
            for (auto* watcher : viewer.findChildren<QFutureWatcherBase*>())
            {
                auto* preload = dynamic_cast<QFutureWatcher<std::pair<QString, QImage>>*>(watcher);
                if (preload != nullptr && preload->isFinished())
                {
                    const QImage result = preload->result().second;
                    return !result.isNull() && result.pixelColor(0, 0) == QColor(Qt::red);
                }
            }
            return false;
        };
        QTRY_VERIFY(cached_neighbor());
        QCoreApplication::sendPostedEvents();
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
    const QByteArray gif = QByteArray::fromBase64(
        "R0lGODlhAgACAIEAAP8AAAAAAAAAAAAAACH/C05FVFNDQVBFMi4wAwEAAAAh+QQACgAAACwAAAAAAgACAAAIBgABCAQQEAAh+QQBCgABACwAAAAAAgACAIEAAP8AAAAAAAAAAAAIBgABCAQQEAA7");
    QCOMPARE(file.write(gif), gif.size());
    file.close();

    image_viewer_window viewer;
    viewer.show();
    viewer.set_image_path(path);
    QPointer<QMovie> movie = viewer.findChild<QMovie*>();
    QVERIFY(!movie.isNull());
    QCOMPARE(movie->frameCount(), 2);
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

int main(int argc, char** argv)
{
    QTemporaryDir settings_dir;
    qputenv("XDG_CONFIG_HOME", settings_dir.path().toUtf8());
    qputenv("XDG_CACHE_HOME", settings_dir.path().toUtf8());
    QImageReader::setAllocationLimit(kQtImageReaderAllocMB);
    QApplication app(argc, argv);
    app.setQuitOnLastWindowClosed(false);
    correctness_test test;
    return QTest::qExec(&test, argc, argv);
}

#include "correctness_test.moc"
