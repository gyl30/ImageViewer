#include <QScrollBar>
#include <QtConcurrent>
#include <QImageReader>
#include <QGraphicsView>
#include <QGraphicsScene>
#include <QGraphicsPixmapItem>
#include <QToolBar>
#include <QAction>
#include <QResizeEvent>
#include <QEvent>
#include <QWheelEvent>
#include <QPushButton>
#include <QFileInfo>
#include <QDir>
#include <QMessageBox>
#include "image_viewer_window.h"

image_viewer_window::image_viewer_window(QWidget* parent)
    : QMainWindow(parent), view_(nullptr), scene_(nullptr), image_item_(nullptr), btn_prev_(nullptr), btn_next_(nullptr)
{
    setup_ui();
    resize(1200, 800);
    setAttribute(Qt::WA_DeleteOnClose);
}

image_viewer_window::~image_viewer_window() = default;

void image_viewer_window::setup_ui()
{
    scene_ = new QGraphicsScene(this);
    view_ = new QGraphicsView(this);
    view_->setScene(scene_);
    view_->setDragMode(QGraphicsView::ScrollHandDrag);
    view_->setRenderHint(QPainter::Antialiasing);
    view_->setRenderHint(QPainter::SmoothPixmapTransform);
    view_->setViewportUpdateMode(QGraphicsView::SmartViewportUpdate);
    view_->setTransformationAnchor(QGraphicsView::AnchorViewCenter);
    view_->setResizeAnchor(QGraphicsView::AnchorViewCenter);
    view_->setBackgroundBrush(Qt::black);

    view_->viewport()->installEventFilter(this);

    setCentralWidget(view_);

    const QString btn_style = R"(
        QPushButton {
            background-color: transparent;
            color: rgba(255, 255, 255, 100);
            font-size: 60px;
            font-weight: bold;
            border: none;
        }
        QPushButton:hover {
            color: white;
        }
    )";

    btn_prev_ = new QPushButton("<", this);
    btn_prev_->setStyleSheet(btn_style);
    btn_prev_->setCursor(Qt::ArrowCursor);

    btn_next_ = new QPushButton(">", this);
    btn_next_->setStyleSheet(btn_style);
    btn_next_->setCursor(Qt::ArrowCursor);

    connect(btn_prev_, &QPushButton::clicked, this, &image_viewer_window::load_prev_image);
    connect(btn_next_, &QPushButton::clicked, this, &image_viewer_window::load_next_image);
}

void image_viewer_window::load_image(const QString& path)
{
    QImageReader reader(path);
    QImageReader::setAllocationLimit(0);
    reader.setAutoTransform(true);
    QImage image = reader.read();
    QMetaObject::invokeMethod(
        this,
        [this, image, path]()
        {
            if (image.isNull())
            {
                setWindowTitle("Error loading image");
                return;
            }

            current_path_ = path;

            scene_->clear();
            QPixmap pixmap = QPixmap::fromImage(image);
            image_item_ = scene_->addPixmap(pixmap);
            scene_->setSceneRect(pixmap.rect());

            view_->fitInView(image_item_, Qt::KeepAspectRatio);

            setWindowTitle(QString("Viewer - %1 (%2x%3)").arg(QFileInfo(path).fileName()).arg(pixmap.width()).arg(pixmap.height()));
        });
}

void image_viewer_window::set_image_path(const QString& path)
{
    current_path_ = path;
    setWindowTitle("Loading...");
    load_future_ = QtConcurrent::run([this, path]() { load_image(path); });
}

void image_viewer_window::showEvent(QShowEvent* event) { QMainWindow::showEvent(event); }

void image_viewer_window::resizeEvent(QResizeEvent* event)
{
    QMainWindow::resizeEvent(event);

    if (image_item_ != nullptr)
    {
        view_->fitInView(image_item_, Qt::KeepAspectRatio);
    }

    int btn_w = 80;
    int btn_h = 150;
    int y_pos = (height() - btn_h) / 2;

    btn_prev_->setGeometry(0, y_pos, btn_w, btn_h);
    btn_next_->setGeometry(width() - btn_w, y_pos, btn_w, btn_h);

    btn_prev_->raise();
    btn_next_->raise();
}

bool image_viewer_window::eventFilter(QObject* watched, QEvent* event)
{
    if (watched == view_->viewport() && event->type() == QEvent::Wheel)
    {
        auto* wheel_event = static_cast<QWheelEvent*>(event);

        if ((wheel_event->modifiers() & Qt::ControlModifier) != 0U)
        {
            if (wheel_event->angleDelta().y() > 0)
            {
                zoom_in();
            }
            else
            {
                zoom_out();
            }
            return true;
        }
    }
    return QMainWindow::eventFilter(watched, event);
}

void image_viewer_window::zoom_in() { view_->scale(1.2, 1.2); }

void image_viewer_window::zoom_out() { view_->scale(1.0 / 1.2, 1.0 / 1.2); }

void image_viewer_window::load_prev_image() { navigate_image(-1); }

void image_viewer_window::load_next_image() { navigate_image(1); }

void image_viewer_window::navigate_image(int delta)
{
    if (current_path_.isEmpty())
    {
        return;
    }

    QFileInfo current_info(current_path_);
    QDir dir = current_info.dir();

    QStringList filters;
    filters << "*.jpg" << "*.jpeg" << "*.png" << "*.bmp" << "*.webp";

    QStringList files = dir.entryList(filters, QDir::Files, QDir::Name);

    if (files.isEmpty())
    {
        return;
    }

    int current_idx = static_cast<int>(files.indexOf(current_info.fileName()));

    if (current_idx == -1)
    {
        return;
    }

    int new_idx = current_idx + delta;

    if (new_idx < 0)
    {
        QMessageBox::information(this, "Info", "这也是第一张图片了。");
        return;
    }
    if (new_idx >= files.count())
    {
        QMessageBox::information(this, "Info", "这也是最后一张图片了。");
        return;
    }

    QString new_path = dir.absoluteFilePath(files[new_idx]);

    set_image_path(new_path);
}
