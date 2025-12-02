#ifndef COMMON_TYPES_H
#define COMMON_TYPES_H

#include <QString>
#include <QSize>

struct image_meta
{
    QString path;
    QSize original_size;
};

const int kColumnMargin = 10;
const int kItemMargin = 10;
const int kThumbWidth = 200;

#endif    // COMMON_TYPES_H
