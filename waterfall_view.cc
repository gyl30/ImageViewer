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

    connect(debounce_timer_, &QTimer::timeout, this, &waterfall_view::on_interaction_finished);
}

void waterfall_view::resizeEvent(QResizeEvent* event)
{
    QGraphicsView::resizeEvent(event);

    debounce_timer_->start();
}

void waterfall_view::scrollContentsBy(int dx, int dy)
{
    QGraphicsView::scrollContentsBy(dx, dy);
    debounce_timer_->start();
}

void waterfall_view::on_interaction_finished()
{
    const int current_width = viewport()->width();
    if (current_width != last_width_ && current_width > 0)
    {
        last_width_ = current_width;
        emit view_resized(current_width);
    }
    check_visible_area();
}
void waterfall_view::check_visible_area()
{
    auto* wf_scene = qobject_cast<waterfall_scene*>(scene());
    if (wf_scene == nullptr)
    {
        return;
    }
    const QRect view_rect = viewport()->rect();
    QRectF scene_rect = mapToScene(view_rect).boundingRect();
    wf_scene->update_viewport(scene_rect);
}
