#include <QEvent>
#include <QHBoxLayout>
#include <QIcon>
#include <QMainWindow>
#include <QMouseEvent>
#include <QStatusBar>
#include <QToolBar>
#include <QToolButton>
#include <QWindow>
#include "window_title_bar.h"

namespace
{
constexpr int kResizeMargin = 5;
}

window_title_bar::window_title_bar(QMainWindow* owner, QToolBar* toolbar)
    : QFrame(owner), toolbar_(toolbar)
{
    setObjectName("window_title_bar");
    owner->setWindowFlag(Qt::FramelessWindowHint);
    owner->setMouseTracking(true);
    setStyleSheet("QFrame#window_title_bar { background: palette(window); border-bottom: 1px solid palette(mid); }");
    auto* layout = new QHBoxLayout(this);
    layout->setContentsMargins(0, 0, 4, 0);
    layout->setSpacing(0);
    layout->addWidget(toolbar, 1);

    auto* controls = new QWidget(this);
    controls->setStyleSheet("QToolButton { border: 0; border-radius: 4px; padding: 8px; } QToolButton:hover { background: palette(alternate-base); }");
    auto* control_layout = new QHBoxLayout(controls);
    control_layout->setContentsMargins(0, 0, 0, 0);
    control_layout->setSpacing(0);
    auto* minimize = new QToolButton(controls);
    minimize->setObjectName("window_minimize");
    minimize->setIcon(QIcon(":/icons/minimize.svg"));
    minimize->setToolTip("最小化");
    minimize->setAccessibleName("最小化");
    minimize->setFixedSize(36, 36);
    control_layout->addWidget(minimize);
    connect(minimize, &QToolButton::clicked, owner, &QWidget::showMinimized);

    maximize_button_ = new QToolButton(controls);
    maximize_button_->setObjectName("window_maximize");
    maximize_button_->setFixedSize(36, 36);
    control_layout->addWidget(maximize_button_);
    connect(maximize_button_, &QToolButton::clicked, this, &window_title_bar::toggle_maximized);

    auto* close = new QToolButton(controls);
    close->setObjectName("window_close");
    close->setIcon(QIcon(":/icons/close.svg"));
    close->setToolTip("关闭");
    close->setAccessibleName("关闭");
    close->setFixedSize(36, 36);
    control_layout->addWidget(close);
    connect(close, &QToolButton::clicked, owner, &QWidget::close);
    layout->addWidget(controls);

    owner->installEventFilter(this);
    toolbar->installEventFilter(this);
    owner->centralWidget()->installEventFilter(this);
    owner->statusBar()->installEventFilter(this);
    installEventFilter(this);
    update_window_controls();
}

void window_title_bar::toggle_maximized()
{
    if (window()->isFullScreen()) { return; }
    if (window()->isMaximized()) { window()->showNormal(); }
    else { window()->showMaximized(); }
}

void window_title_bar::update_window_controls()
{
    const bool maximized = window()->isMaximized();
    const bool full_screen = window()->isFullScreen();
    maximize_button_->setIcon(QIcon(maximized ? ":/icons/restore.svg" : ":/icons/maximize.svg"));
    maximize_button_->setToolTip(maximized ? "还原" : "最大化");
    maximize_button_->setAccessibleName(maximize_button_->toolTip());
    maximize_button_->setEnabled(!full_screen);
    const int margin = maximized || full_screen ? 0 : kResizeMargin;
    window()->setContentsMargins(margin, margin, margin, margin);
    window()->unsetCursor();
}

bool window_title_bar::eventFilter(QObject* watched, QEvent* event)
{
    auto* owner = window();
    if (watched == owner && event->type() == QEvent::WindowStateChange)
    {
        update_window_controls();
    }
    if (watched != owner && event->type() == QEvent::Enter)
    {
        owner->unsetCursor();
    }
    if ((watched == this || watched == toolbar_) && event->type() == QEvent::MouseButtonDblClick)
    {
        if (static_cast<QMouseEvent*>(event)->button() == Qt::LeftButton)
        {
            toggle_maximized();
            return true;
        }
    }
    if ((watched == this || watched == toolbar_) && event->type() == QEvent::MouseButtonPress)
    {
        if (static_cast<QMouseEvent*>(event)->button() == Qt::LeftButton)
        {
            owner->windowHandle()->startSystemMove();
            return true;
        }
    }
    if (watched == owner && !owner->isMaximized() && !owner->isFullScreen() &&
        (event->type() == QEvent::MouseMove || event->type() == QEvent::MouseButtonPress))
    {
        auto* mouse = static_cast<QMouseEvent*>(event);
        const QPointF pos = mouse->position();
        Qt::Edges edges;
        if (pos.x() < kResizeMargin) { edges |= Qt::LeftEdge; }
        if (pos.x() >= owner->width() - kResizeMargin) { edges |= Qt::RightEdge; }
        if (pos.y() < kResizeMargin) { edges |= Qt::TopEdge; }
        if (pos.y() >= owner->height() - kResizeMargin) { edges |= Qt::BottomEdge; }
        if (event->type() == QEvent::MouseMove)
        {
            if (edges == (Qt::LeftEdge | Qt::TopEdge) || edges == (Qt::RightEdge | Qt::BottomEdge))
            {
                owner->setCursor(Qt::SizeFDiagCursor);
            }
            else if (edges == (Qt::RightEdge | Qt::TopEdge) || edges == (Qt::LeftEdge | Qt::BottomEdge))
            {
                owner->setCursor(Qt::SizeBDiagCursor);
            }
            else if (edges.testFlag(Qt::LeftEdge) || edges.testFlag(Qt::RightEdge))
            {
                owner->setCursor(Qt::SizeHorCursor);
            }
            else if (edges.testFlag(Qt::TopEdge) || edges.testFlag(Qt::BottomEdge))
            {
                owner->setCursor(Qt::SizeVerCursor);
            }
            else { owner->unsetCursor(); }
        }
        else if (mouse->button() == Qt::LeftButton && edges != Qt::Edges())
        {
            return owner->windowHandle()->startSystemResize(edges);
        }
    }
    return QFrame::eventFilter(watched, event);
}
