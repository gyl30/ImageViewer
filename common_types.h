#ifndef COMMON_TYPES_H
#define COMMON_TYPES_H

#include <QString>
#include <QSize>
#include <QList>
#include <QRectF>
#include <QMetaType>

struct image_meta
{
    QString path;
    QSize original_size;
};

struct layout_model
{
    int index;
    QString path;
    QSize original_size;
    QRectF layout_rect;
};

struct load_task
{
    quint64 id;
    QString path;
    QSize target_size;
    int session_id;
};

Q_DECLARE_METATYPE(load_task)
Q_DECLARE_METATYPE(QList<load_task>)

const int kColumnMargin = 10;
const int kItemMargin = 10;
const int kMinColWidth = 200;

#endif
