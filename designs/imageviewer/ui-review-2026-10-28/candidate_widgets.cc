// 仅用于本轮Qt候选比较，不编入正式程序。
#include "candidate_widgets.h"
#include "main_window.h"
#include "image_viewer_window.h"
#include "waterfall_scene.h"
#include "waterfall_item.h"
#include <QAction>
#include <QApplication>
#include <QFileInfo>
#include <QGraphicsView>
#include <QHideEvent>
#include <QHBoxLayout>
#include <QLabel>
#include <QMenu>
#include <QPainter>
#include <QPointer>
#include <QShowEvent>
#include <QStatusBar>
#include <QToolBar>
#include <QToolButton>
#include <QVBoxLayout>
#include <array>
#include <algorithm>

namespace
{
constexpr int kStripCells = 7;

bool enhanced() { return qEnvironmentVariable("IV_UI_VARIANT") == "b"; }
bool baseline() { return qEnvironmentVariable("IV_UI_VARIANT", "baseline") == "baseline"; }

QPalette review_palette()
{
    const bool dark = QApplication::palette().base().color().lightness() < 128;
    QPalette palette = QApplication::palette();
    palette.setColor(QPalette::Window, QColor(dark ? "#191d24" : "#f4f6f8"));
    palette.setColor(QPalette::Base, QColor(dark ? "#191d24" : "#f4f6f8"));
    palette.setColor(QPalette::AlternateBase, QColor(dark ? "#282f3a" : "#e7ecf2"));
    palette.setColor(QPalette::Text, QColor(dark ? "#e6ecf5" : "#28374a"));
    palette.setColor(QPalette::WindowText, palette.color(QPalette::Text));
    palette.setColor(QPalette::ButtonText, palette.color(QPalette::Text));
    palette.setColor(QPalette::Mid, QColor(dark ? "#3c4758" : "#ccd5e0"));
    palette.setColor(QPalette::Highlight, QColor(dark ? "#65adff" : "#246fc4"));
    palette.setColor(QPalette::Disabled, QPalette::ButtonText, QColor(dark ? "#68768a" : "#8491a3"));
    return palette;
}

void style_window(QMainWindow& window)
{
    const QPalette palette = review_palette();
    QApplication::setPalette(palette);
    window.setPalette(palette);
    const QString background = palette.color(QPalette::Window).name();
    const QString border = palette.color(QPalette::Mid).name();
    const QString hover = palette.color(QPalette::AlternateBase).name();
    const QString accent = palette.color(QPalette::Highlight).name();
    auto* header = window.findChild<QWidget*>("window_title_bar");
    header->setPalette(palette);
    header->setStyleSheet(QString("QFrame#window_title_bar { background: %1; border-bottom: 1px solid %2; }")
                              .arg(background, border));
    for (auto* child : header->findChildren<QWidget*>(QString(), Qt::FindDirectChildrenOnly))
    {
        if (qobject_cast<QToolBar*>(child) != nullptr) { continue; }
        child->setStyleSheet(QString("QToolButton { border: 0; border-radius: 5px; padding: 8px; } QToolButton:hover { background: %1; }").arg(hover));
    }
    for (auto* toolbar : header->findChildren<QToolBar*>())
    {
        toolbar->setPalette(palette);
        toolbar->setIconSize(QSize(20, 20));
        toolbar->setStyleSheet(QString("QToolBar { background: %1; border: 0; spacing: 2px; padding: 4px; }"
                                      "QToolButton { padding: 6px; border: 1px solid transparent; border-radius: 5px; }"
                                      "QToolButton::menu-button { background: transparent; border: 0; width: 12px; }"
                                      "QToolButton:hover, QToolButton:checked { background: %2; }"
                                      "QToolButton:focus { border-color: %3; }")
                                  .arg(background, hover, accent));
    }
    for (auto* button : header->findChildren<QToolButton*>())
    {
        if (button->objectName() == "qt_toolbar_ext_button") { continue; }
        button->setIconSize(QSize(20, 20));
        const qreal dpr = button->devicePixelRatioF();
        QPixmap pixmap = button->icon().pixmap(QSize(20, 20) * dpr);
        if (pixmap.isNull()) { continue; }
        QPainter painter(&pixmap);
        painter.setCompositionMode(QPainter::CompositionMode_SourceIn);
        painter.fillRect(pixmap.rect(), palette.color(QPalette::ButtonText));
        painter.end();
        pixmap.setDevicePixelRatio(dpr);
        button->setIcon(QIcon(pixmap));
    }
    for (auto* view : window.findChildren<QGraphicsView*>())
    {
        view->setPalette(palette);
        view->setBackgroundBrush(palette.base());
        view->setFrameShape(QFrame::NoFrame);
        view->viewport()->update();
    }
    window.statusBar()->setPalette(palette);
    window.statusBar()->setStyleSheet(QString("QStatusBar { background: %1; border-top: 1px solid %2; } QStatusBar::item { border: 0; }")
                                         .arg(background, border));
}

class filmstrip : public QWidget
{
   public:
    filmstrip(image_viewer_window& viewer, waterfall_scene& scene) : QWidget(&viewer), viewer_(viewer), scene_(&scene)
    {
        setObjectName("review_filmstrip");
        setFixedHeight(84);
        auto* column = new QVBoxLayout(this);
        column->setContentsMargins(12, 4, 12, 4);
        column->setSpacing(2);
        auto* label = new QLabel("邻近图片", this);
        column->addWidget(label, 0, Qt::AlignCenter);
        auto* row = new QHBoxLayout();
        row->setSpacing(6);
        row->addStretch();
        for (auto*& button : buttons_)
        {
            button = new QToolButton(this);
            button->setFixedSize(76, 56);
            button->setIconSize(QSize(64, 48));
            button->setToolButtonStyle(Qt::ToolButtonIconOnly);
            button->setCheckable(true);
            button->setStyleSheet("QToolButton { border: 2px solid transparent; border-radius: 5px; padding: 2px; }"
                                 "QToolButton:checked { border-color: palette(highlight); }"
                                 "QToolButton:hover, QToolButton:focus { background: palette(alternate-base); }");
            connect(button, &QToolButton::clicked, this, [this, button]()
            {
                viewer_.set_image_path(button->property("review_path").toString());
            });
            row->addWidget(button);
        }
        row->addStretch();
        column->addLayout(row);
        connect(&viewer_, &image_viewer_window::current_image_changed, this, [this]() { refresh(); });
        connect(&scene, &QGraphicsScene::changed, this, [this]() { if (isVisible()) { refresh(); } });
    }

   protected:
    void showEvent(QShowEvent* event) override { QWidget::showEvent(event); refresh(); }
    void hideEvent(QHideEvent* event) override
    {
        for (auto* button : buttons_) { button->setIcon(QIcon()); }
        QWidget::hideEvent(event);
    }

   private:
    void refresh()
    {
        if (!isVisible() || scene_.isNull()) { return; }
        const QStringList paths = scene_->get_all_paths();
        const qsizetype current = paths.indexOf(viewer_.current_image_path());
        if (current < 0) { return; }
        const qsizetype first = std::clamp(current - kStripCells / 2, qsizetype(0), std::max(qsizetype(0), paths.size() - kStripCells));
        const auto items = scene_->items();
        for (int slot = 0; slot < kStripCells; ++slot)
        {
            auto* button = buttons_[slot];
            const qsizetype index = first + slot;
            button->setVisible(index < paths.size());
            if (index >= paths.size()) { continue; }
            const QString& path = paths[index];
            button->setProperty("review_path", path);
            button->setToolTip(QFileInfo(path).fileName());
            button->setChecked(index == current);
            QPixmap thumbnail;
            for (auto* graphic : items)
            {
                auto* item = dynamic_cast<waterfall_item*>(graphic);
                if (item != nullptr && item->get_path() == path &&
                    (item->state() == waterfall_item::load_state::ready || item->state() == waterfall_item::load_state::refreshing))
                {
                    thumbnail = item->pixmap().scaled(QSize(64, 48) * devicePixelRatioF(), Qt::KeepAspectRatio, Qt::SmoothTransformation);
                    thumbnail.setDevicePixelRatio(devicePixelRatioF());
                    break;
                }
            }
            button->setIcon(thumbnail.isNull() ? QIcon(":/icons/image.svg") : QIcon(thumbnail));
        }
    }

    image_viewer_window& viewer_;
    QPointer<waterfall_scene> scene_;
    std::array<QToolButton*, kStripCells> buttons_{};
};
}

void review_library(main_window& window, const QString& directory)
{
    if (baseline()) { return; }
    style_window(window);
    auto* context = window.findChild<QLabel*>("review_directory");
    if (context == nullptr)
    {
        context = new QLabel(&window);
        context->setObjectName("review_directory");
        context->setTextFormat(Qt::PlainText);
        context->setMaximumWidth(200);
        if (enhanced())
        {
            auto* toolbar = window.findChild<QToolBar*>("browse_toolbar");
            toolbar->insertWidget(toolbar->actions().at(2), context);
        }
        else { window.statusBar()->insertWidget(0, context); }
    }
    const QString name = QFileInfo(directory).fileName();
    context->setText(context->fontMetrics().elidedText(name, Qt::ElideMiddle, 180) + "  ·  ");
    context->setToolTip(name);
}

void review_preview(image_viewer_window& window, waterfall_scene& scene)
{
    if (baseline()) { return; }
    style_window(window);
    if (!enhanced()) { return; }
    auto* container = new QWidget(&window);
    auto* layout = new QVBoxLayout(container);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    layout->addWidget(window.takeCentralWidget(), 1);
    auto* strip = new filmstrip(window, scene);
    layout->addWidget(strip);
    window.setCentralWidget(container);
    for (auto* button : window.findChildren<QToolButton*>())
    {
        if (button->text() != "更多" || button->menu() == nullptr) { continue; }
        button->menu()->addSeparator();
        auto* toggle = button->menu()->addAction("缩略图导航");
        toggle->setCheckable(true);
        toggle->setChecked(true);
        QObject::connect(toggle, &QAction::toggled, strip, &QWidget::setVisible);
        break;
    }
}
