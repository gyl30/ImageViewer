#include <QApplication>
#include <QGraphicsView>
#include <QImageReader>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QThread>
#include <QtTest>
#include "common_types.h"
#include "image_loader.h"
#include "waterfall_item.h"
#include "waterfall_scene.h"

class loader_worker
{
   public:
    loader_worker()
    {
        loader = new image_loader();
        loader->moveToThread(&thread);
        QObject::connect(&thread, &QThread::started, loader, &image_loader::start_loop);
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

class correctness_test : public QObject
{
    Q_OBJECT

   private slots:
    void large_visible_batch();
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
