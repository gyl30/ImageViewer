#ifndef IMAGE_VIEWER_IMAGE_LOADER_H
#define IMAGE_VIEWER_IMAGE_LOADER_H

#include <QObject>
#include <QImage>
#include <QCache>
#include <QList>
#include <QSize>
#include <QSet>
#include "common_types.h"

class image_loader : public QObject
{
    Q_OBJECT

   public:
    explicit image_loader(QObject* parent = nullptr);
    ~image_loader() override;

   public slots:
    void request_thumbnails(const QList<load_task>& tasks);
    void cancel_thumbnails(const QList<QString>& paths);
    void clear_all();

   signals:
    void thumbnail_loaded(quint64 id, QString path, QImage image, int session_id);

   private slots:
    void process_next_task();

   private:
    QCache<QString, QImage> cache_;
    QList<load_task> task_queue_;
    QSet<QString> pending_cancels_;
    bool is_processing_ = false;
};

#endif
