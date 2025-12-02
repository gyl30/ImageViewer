#include <QScrollBar>
#include <QResizeEvent>
#include "waterfall_view.h"

waterfall_view::waterfall_view(QWidget* parent) : QGraphicsView(parent)
{
    setViewportUpdateMode(QGraphicsView::SmartViewportUpdate);
    setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    setDragMode(QGraphicsView::ScrollHandDrag);
}

void waterfall_view::resizeEvent(QResizeEvent* event)
{
    QGraphicsView::resizeEvent(event);

    int content_width = event->size().width();
    if (verticalScrollBar()->isVisible())
    {
        content_width -= verticalScrollBar()->width();
    }

    emit view_resized(content_width);
}
