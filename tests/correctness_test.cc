#include <QApplication>
#include <QGraphicsView>
#include <QImageReader>
#include <QFile>
#include <QFileInfo>
#include <QStandardPaths>
#include <QSemaphore>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QThread>
#include <QtTest>
#include <QtConcurrent>
#include "common_types.h"
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
