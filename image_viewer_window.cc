#include <QScrollBar>
#include <QWheelEvent>
#include <QMessageBox>
#include <QtConcurrent>
#include <QImageReader>
#include <QGraphicsView>
#include <QGraphicsScene>
#include <QGraphicsPixmapItem>
#include "image_viewer_window.h"

image_viewer_window::image_viewer_window(QWidget* parent) : QMainWindow(parent), view_(nullptr), scene_(nullptr), image_item_(nullptr)
{
    setup_ui();
    resize(1200, 800);
    setAttribute(Qt::WA_DeleteOnClose);
}

image_viewer_window::~image_viewer_window() {}

void image_viewer_window::setup_ui()
{
    scene_ = new QGraphicsScene(this);
    view_ = new QGraphicsView(this);
    view_->setScene(scene_);
    view_->setDragMode(QGraphicsView::ScrollHandDrag);
    view_->setRenderHint(QPainter::Antialiasing);
    view_->setRenderHint(QPainter::SmoothPixmapTransform);
    view_->setViewportUpdateMode(QGraphicsView::SmartViewportUpdate);
    view_->setTransformationAnchor(QGraphicsView::AnchorUnderMouse);
    view_->setResizeAnchor(QGraphicsView::AnchorUnderMouse);
    view_->setBackgroundBrush(Qt::black);
    setCentralWidget(view_);
}

void image_viewer_window::load_image(const QString& path)
{
    QImageReader reader(path);
    QImageReader::setAllocationLimit(0);

    QImage image = reader.read();
    QMetaObject::invokeMethod(this,
                              [this, image]()
                              {
                                  if (image.isNull())
                                  {
                                      setWindowTitle("Error loading image");
                                      return;
                                  }
                                  scene_->clear();
                                  QPixmap pixmap = QPixmap::fromImage(image);
                                  image_item_ = scene_->addPixmap(pixmap);
                                  scene_->setSceneRect(pixmap.rect());
                                  setWindowTitle(QString("Viewer - %1 (%2x%3)").arg(current_path_).arg(pixmap.width()).arg(pixmap.height()));
                              });
}

void image_viewer_window::set_image_path(const QString& path)
{
    current_path_ = path;

    setWindowTitle("Loading...");

    load_future_ = QtConcurrent::run([this, path]() { load_image(path); });
}

void image_viewer_window::showEvent(QShowEvent* event) { QMainWindow::showEvent(event); }

void image_viewer_window::wheelEvent(QWheelEvent* event)
{
    const double scale_factor = 1.15;

    if (event->angleDelta().y() > 0)
    {
        view_->scale(scale_factor, scale_factor);
    }
    else
    {
        view_->scale(1.0 / scale_factor, 1.0 / scale_factor);
    }
    QMainWindow::wheelEvent(event);
}
