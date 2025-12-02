#ifndef IMAGE_LOADER_H
#define IMAGE_LOADER_H

#include <QObject>
#include <QImage>
#include <QMutex>
#include <QWaitCondition>
#include <QCache>
#include <QSet>
#include <QList>
#include <QSize>
#include <atomic>

struct load_task
{
    QString path;
    QSize target_size;
};

class image_loader : public QObject
{
    Q_OBJECT

   public:
    explicit image_loader(QObject* parent = nullptr);
    ~image_loader() override;

    void start_processing();
    void stop_processing();

   public slots:
    void request_thumbnail(const QString& path, const QSize& target_size);
    void cancel_thumbnail(const QString& path);

   signals:
    void thumbnail_loaded(QString path, QImage image);

   private:
    void process_loop();

   private:
    QCache<QString, QImage> cache_;

    QList<load_task> task_queue_;
    QSet<QString> pending_paths_;

    QMutex mutex_;
    QWaitCondition condition_;
    std::atomic<bool> stop_flag_;

    QString current_loading_path_;
};

#endif
