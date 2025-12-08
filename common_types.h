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

struct layout_result
{
    std::vector<QRectF> rects;
    std::vector<int> col_heights;
    std::vector<int> item_y_index;
    int max_height = 0;
    int view_width = 0;
    size_t count = 0;
};

Q_DECLARE_METATYPE(load_task)
Q_DECLARE_METATYPE(QList<load_task>)
Q_DECLARE_METATYPE(layout_result)

const int kColumnMargin = 10;
const int kItemMargin = 10;
const int kMinColWidth = 200;

#endif
