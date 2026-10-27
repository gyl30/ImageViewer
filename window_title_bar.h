#ifndef WINDOW_TITLE_BAR_H
#define WINDOW_TITLE_BAR_H

#include <QFrame>

class QMainWindow;
class QToolBar;
class QToolButton;

class window_title_bar : public QFrame
{
   public:
    window_title_bar(QMainWindow* owner, QToolBar* toolbar);

   protected:
    bool eventFilter(QObject* watched, QEvent* event) override;

   private:
    void toggle_maximized();
    void update_window_controls();

    QToolBar* toolbar_;
    QToolButton* maximize_button_;
};

#endif
