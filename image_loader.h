#ifndef IMAGE_VIEWER_IMAGE_LOADER_H
#define IMAGE_VIEWER_IMAGE_LOADER_H

#include <QObject>
#include <QImage>
#include <QCache>
#include <QList>
#include <QSize>
#include <QSet>
#include <QMutex>
#include <QWaitCondition>
#include <atomic>
#include "common_types.h"

class image_loader : public QObject
{
    Q_OBJECT

   public:
    explicit image_loader(QObject* parent = nullptr);
    ~image_loader() override;

   public slots:
    void start_loop();
    void stop();
    void request_thumbnails(const QList<load_task>& tasks);
    void cancel_thumbnails(const QList<QString>& paths);
    void clear_all();

   signals:
    void thumbnail_loaded(quint64 id, QString path, QImage image, int session_id);
    void tasks_dropped(const QList<QString>& paths);

   private:
    void load_image_internal(const load_task& task);

   private:
    QCache<QString, QImage> cache_;
    QList<load_task> task_queue_;
    QSet<QString> pending_cancels_;

    QMutex mutex_;
    QWaitCondition condition_;
    std::atomic<bool> abort_;
};

#endif
