#ifndef WATERFALL_VIEW_H
#define WATERFALL_VIEW_H

#include <QGraphicsView>

class waterfall_view : public QGraphicsView
{
    Q_OBJECT

public:
    explicit waterfall_view(QWidget* parent = nullptr);

signals:
    void view_resized(int new_width);

protected:
    void resizeEvent(QResizeEvent* event) override;
};

#endif // WATERFALL_VIEW_H
