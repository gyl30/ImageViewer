#include <QScrollBar>
#include <QtConcurrent>
#include <QImageReader>
#include <QGraphicsView>
#include <QGraphicsScene>
#include <QGraphicsPixmapItem>
#include <QToolBar>
#include <QAction>
#include <QResizeEvent>
#include "image_viewer_window.h"

image_viewer_window::image_viewer_window(QWidget* parent) : QMainWindow(parent), view_(nullptr), scene_(nullptr), image_item_(nullptr)
{
    setup_ui();
    resize(800, 1200);
    setAttribute(Qt::WA_DeleteOnClose);
}

image_viewer_window::~image_viewer_window() = default;

void image_viewer_window::setup_ui()
{
    QToolBar* toolbar = addToolBar("Tools");
    toolbar->setMovable(false);

    QAction* act_zoom_in = toolbar->addAction("Zoom In (+)");
    QAction* act_zoom_out = toolbar->addAction("Zoom Out (-)");

    connect(act_zoom_in, &QAction::triggered, this, &image_viewer_window::zoom_in);
    connect(act_zoom_out, &QAction::triggered, this, &image_viewer_window::zoom_out);

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
                                  view_->fitInView(image_item_, Qt::KeepAspectRatio);
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

void image_viewer_window::resizeEvent(QResizeEvent* event)
{
    QMainWindow::resizeEvent(event);

    if (image_item_ != nullptr)
    {
        view_->fitInView(image_item_, Qt::KeepAspectRatio);
    }
}

void image_viewer_window::zoom_in() { view_->scale(1.2, 1.2); }

void image_viewer_window::zoom_out() { view_->scale(1.0 / 1.2, 1.0 / 1.2); }
