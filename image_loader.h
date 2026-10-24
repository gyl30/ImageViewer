#ifndef IMAGE_VIEWER_IMAGE_LOADER_H
#define IMAGE_VIEWER_IMAGE_LOADER_H

#include <QObject>
#include <QImage>
#include <QCache>
#include <QList>
#include <QSize>
#include <QMutex>
#include <QString>
#include <atomic>
#include "common_types.h"

class image_loader : public QObject
{
    Q_OBJECT
    friend class performance_probe;
    friend class correctness_test;

   public:
    explicit image_loader(QObject* parent = nullptr);
    ~image_loader() override;

   public slots:
    void stop();
    void request_thumbnails(const QList<load_task>& tasks);
    void cancel_thumbnails(const QList<QString>& paths);
    void clear_all();
    void clear_cache();

   signals:
    void thumbnail_loaded(quint64 id, QString path, QImage image, int session_id);

   private:
    void load_next();
    [[nodiscard]] QString disk_cache_path(const load_task& task) const;
    void cleanup_disk_cache();
    void load_image_internal(const load_task& task);

   private:
    QCache<QString, QImage> cache_;
    QList<load_task> task_queue_;
    QString disk_cache_dir_;
    qint64 disk_cache_bytes_ = 0;
    qsizetype disk_cache_files_ = 0;

    QMutex mutex_;
    bool is_loading_ = false;
    std::atomic<bool> abort_;
};

#endif
