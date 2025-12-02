#include <QImageReader>
#include <QDebug>
#include "image_loader.h"

image_loader::image_loader(QObject* parent) : QObject(parent) { cache_.setMaxCost(50L * 1024 * 1024); }

image_loader::~image_loader() { cache_.clear(); }

void image_loader::request_thumbnail(const QString& path, const QSize& target_size)
{
    if (cache_.contains(path))
    {
        emit thumbnail_loaded(path, *cache_.object(path));
        return;
    }

    if (path.isEmpty())
    {
        return;
    }

    QImageReader reader(path);

    if (reader.supportsOption(QImageIOHandler::ScaledSize))
    {
        reader.setScaledSize(target_size);
    }

    QImage image = reader.read();

    if (image.isNull())
    {
        return;
    }

    if (image.width() != target_size.width())
    {
        image = image.scaled(target_size, Qt::KeepAspectRatio, Qt::SmoothTransformation);
    }

    QPixmap pixmap = QPixmap::fromImage(image);

    int cost = pixmap.width() * pixmap.height() * 4;
    cache_.insert(path, new QPixmap(pixmap), cost);
    emit thumbnail_loaded(path, pixmap);
}
