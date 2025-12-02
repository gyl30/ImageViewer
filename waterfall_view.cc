#include <QScrollBar>
#include <QResizeEvent>
#include "waterfall_view.h"
#include "waterfall_scene.h"

waterfall_view::waterfall_view(QWidget* parent) : QGraphicsView(parent)
{
    setViewportUpdateMode(QGraphicsView::SmartViewportUpdate);
    setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    setDragMode(QGraphicsView::ScrollHandDrag);

    debounce_timer_ = new QTimer(this);
    debounce_timer_->setSingleShot(true);
    debounce_timer_->setInterval(150);

    connect(debounce_timer_, &QTimer::timeout, this, &waterfall_view::check_visible_area);
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

    debounce_timer_->start();
}

void waterfall_view::scrollContentsBy(int dx, int dy)
{
    QGraphicsView::scrollContentsBy(dx, dy);

    debounce_timer_->start();
}

void waterfall_view::check_visible_area()
{
    auto* wf_scene = qobject_cast<waterfall_scene*>(scene());
    if (wf_scene == nullptr)
    {
        return;
    }

    QRect view_rect = viewport()->rect();
    QRectF scene_rect = mapToScene(view_rect).boundingRect();

    scene_rect.adjust(0, -500, 0, 500);

    wf_scene->load_visible_items(scene_rect);
}
