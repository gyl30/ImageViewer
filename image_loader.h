#ifndef IMAGE_VIEWER_IMAGE_LOADER_H
#define IMAGE_VIEWER_IMAGE_LOADER_H

#include <QObject>
#include <QImage>
#include <QCache>
#include <QList>
#include <QSize>

struct load_task
{
    quint64 id;
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
    void request_thumbnail(quint64 id, const QString& path, const QSize& target_size);
    void cancel_thumbnail(const QString& path);
    void clear_all();

   signals:
    void thumbnail_loaded(quint64 id, QString path, QImage image);

   private slots:
    void process_next_task();

   private:
    QCache<QString, QImage> cache_;

    QList<load_task> task_queue_;

    bool is_processing_ = false;
};

#endif
