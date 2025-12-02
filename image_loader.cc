#include <QImageReader>
#include <QDebug>
#include "image_loader.h"

image_loader::image_loader(QObject* parent) : QObject(parent) { cache_.setMaxCost(100L * 1024 * 1024); }

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
    reader.setAutoTransform(true);

    if (reader.supportsOption(QImageIOHandler::ScaledSize))
    {
        reader.setScaledSize(target_size);
    }

    QImage image;

    if (reader.canRead())
    {
        image = reader.read();
    }

    if (image.isNull())
    {
        return;
    }

    if (image.width() != target_size.width() && !target_size.isEmpty())
    {
        image = image.scaled(target_size, Qt::KeepAspectRatio, Qt::SmoothTransformation);
    }

    int cost = (image.width() * image.height() * 4);

    cache_.insert(path, new QImage(image), cost);
    emit thumbnail_loaded(path, image);
}
