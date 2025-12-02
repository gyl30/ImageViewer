#ifndef IMAGE_LOADER_H
#define IMAGE_LOADER_H

#include <QObject>
#include <QImage>
#include <QMutex>
#include <QCache>
#include <QSize>

class image_loader : public QObject
{
    Q_OBJECT

   public:
    explicit image_loader(QObject* parent = nullptr);
    ~image_loader() override;

   public slots:
    void request_thumbnail(const QString& path, const QSize& target_size);

   signals:
    void thumbnail_loaded(QString path, QImage image);

   private:
    QCache<QString, QImage> cache_;
};

#endif    // IMAGE_LOADER_H
