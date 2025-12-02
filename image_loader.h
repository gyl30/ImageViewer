#ifndef IMAGE_LOADER_H
#define IMAGE_LOADER_H

#include <QObject>
#include <QPixmap>
#include <QMutex>
#include <QCache>

class image_loader : public QObject
{
    Q_OBJECT

   public:
    explicit image_loader(QObject* parent = nullptr);
    ~image_loader() override;

   public slots:
    void request_thumbnail(const QString& path, const QSize& target_size);

   signals:
    void thumbnail_loaded(QString path, QPixmap pixmap);

   private:
    QCache<QString, QPixmap> cache_;
};

#endif    // IMAGE_LOADER_H
