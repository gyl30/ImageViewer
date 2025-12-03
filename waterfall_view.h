#ifndef WATERFALL_VIEW_H
#define WATERFALL_VIEW_H

#include <QTimer>
#include <QGraphicsView>

class waterfall_view : public QGraphicsView
{
    Q_OBJECT

   public:
    explicit waterfall_view(QWidget* parent = nullptr);

    void check_visible_area();

   signals:
    void view_resized(int new_width);

   protected:
    void resizeEvent(QResizeEvent* event) override;
    void scrollContentsBy(int dx, int dy) override;

   private slots:
    void on_interaction_finished();

   private:
    QTimer* debounce_timer_;
    int last_width_ = -1;
};

#endif
