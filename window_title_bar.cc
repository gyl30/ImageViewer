#include <QEvent>
#include <QHBoxLayout>
#include <QIcon>
#include <QIconEngine>
#include <QApplication>
#include <QPainter>
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

class toolbar_icon_engine : public QIconEngine
{
   public:
    explicit toolbar_icon_engine(const QIcon& source) : source_(source) {}
    QIconEngine* clone() const override { return new toolbar_icon_engine(source_); }
    bool isNull() override { return source_.isNull(); }

    QPixmap pixmap(const QSize& size, QIcon::Mode mode, QIcon::State state) override
    {
        // QIconEngine此处已收到目标像素尺寸，不能再应用屏幕倍率。
        QPixmap result = source_.pixmap(size, 1.0, QIcon::Normal, state);
        if (result.isNull()) { return result; }
        const QPalette palette = QApplication::palette();
        const auto group = mode == QIcon::Disabled ? QPalette::Disabled : QPalette::Active;
        const auto role = mode == QIcon::Selected ? QPalette::HighlightedText : QPalette::ButtonText;
        QPainter painter(&result);
        painter.setCompositionMode(QPainter::CompositionMode_SourceIn);
        painter.fillRect(result.rect(), palette.color(group, role));
        return result;
    }

    void paint(QPainter* painter, const QRect& rect, QIcon::Mode mode, QIcon::State state) override
    {
        const qreal dpr = painter->device()->devicePixelRatioF();
        painter->drawPixmap(rect, pixmap(rect.size() * dpr, mode, state));
    }

   private:
    QIcon source_;
};
}

QIcon toolbar_icon(const QString& path)
{
    return QIcon(new toolbar_icon_engine(QIcon(path)));
}

window_title_bar::window_title_bar(QMainWindow* owner, QToolBar* toolbar)
    : QFrame(owner), toolbar_(toolbar)
{
    setObjectName("window_title_bar");
    owner->setWindowFlag(Qt::FramelessWindowHint);
    owner->setMouseTracking(true);
    setAutoFillBackground(true);
    toolbar->setIconSize(QSize(20, 20));
    toolbar->setToolButtonStyle(Qt::ToolButtonIconOnly);
    for (auto* button : toolbar->findChildren<QToolButton*>()) { button->setIconSize(QSize(20, 20)); }
    auto* layout = new QHBoxLayout(this);
    layout->setContentsMargins(0, 0, 4, 0);
    layout->setSpacing(0);
    layout->addWidget(toolbar, 1);

    auto* controls = new QWidget(this);
    auto* control_layout = new QHBoxLayout(controls);
    control_layout->setContentsMargins(0, 0, 0, 0);
    control_layout->setSpacing(0);
    auto* minimize = new QToolButton(controls);
    minimize->setObjectName("window_minimize");
    minimize->setIcon(toolbar_icon(":/icons/minimize.svg"));
    minimize->setIconSize(QSize(20, 20));
    minimize->setToolTip("最小化");
    minimize->setAccessibleName("最小化");
    minimize->setFixedSize(36, 36);
    control_layout->addWidget(minimize);
    connect(minimize, &QToolButton::clicked, owner, &QWidget::showMinimized);

    maximize_button_ = new QToolButton(controls);
    maximize_button_->setObjectName("window_maximize");
    maximize_button_->setFixedSize(36, 36);
    maximize_button_->setIconSize(QSize(20, 20));
    control_layout->addWidget(maximize_button_);
    connect(maximize_button_, &QToolButton::clicked, this, &window_title_bar::toggle_maximized);

    auto* close = new QToolButton(controls);
    close->setObjectName("window_close");
    close->setIcon(toolbar_icon(":/icons/close.svg"));
    close->setIconSize(QSize(20, 20));
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
    update_palette();
    update_window_controls();
}

void window_title_bar::update_palette()
{
    const QPalette palette = QApplication::palette();
    const QString hover = palette.color(QPalette::AlternateBase).name();
    const QString accent = palette.color(QPalette::Highlight).name();
    toolbar_->setStyleSheet(QString(
        "QToolBar { background: transparent; border: 0; spacing: 2px; padding: 4px 2px; }"
        "QToolButton { padding: 6px; border: 1px solid transparent; border-radius: 5px; }"
        "QToolButton::menu-button { background: transparent; border: 0; width: 12px; }"
        "QToolButton:hover, QToolButton:checked { background: %1; }"
        "QToolButton:focus { border-color: %2; }"
        "QToolButton::menu-indicator, QToolButton::menu-arrow { image: none; }")
        .arg(hover, accent));
    maximize_button_->parentWidget()->setStyleSheet(QString(
        "QToolButton { border: 0; border-radius: 5px; padding: 8px; }"
        "QToolButton:hover { background: %1; } QToolButton:focus { background: %1; }").arg(hover));
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
    maximize_button_->setIcon(toolbar_icon(maximized ? ":/icons/restore.svg" : ":/icons/maximize.svg"));
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
    if (watched == owner && event->type() == QEvent::PaletteChange) { update_palette(); }
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
