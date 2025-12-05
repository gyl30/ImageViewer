#ifndef IMAGE_VIEWER_IMAGE_LOADER_H
#define IMAGE_VIEWER_IMAGE_LOADER_H

#include <QObject>
#include <QImage>
#include <QCache>
#include <QList>
#include <QSet>
#include <QSize>

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

   public slots:

    void request_thumbnail(const QString& path, const QSize& target_size);
    void cancel_thumbnail(const QString& path);

    void clear_all();

   signals:

    void thumbnail_loaded(QString path, QImage image);

   private slots:

    void process_next_task();

   private:
    QCache<QString, QImage> cache_;

    QList<load_task> task_queue_;

    QSet<QString> pending_paths_;

    bool is_processing_ = false;
};

#endif
