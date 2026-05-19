#include <cmath>
#include <array>
#include <algorithm>
#include <QDir>
#include <QScrollBar>
#include <QtConcurrent>
#include <QFutureWatcher>
#include <QImageReader>
#include <QGraphicsView>
#include <QGraphicsScene>
#include <QGraphicsPixmapItem>
#include <QGraphicsTextItem>
#include <QToolBar>
#include <QAction>
#include <QActionGroup>
#include <QResizeEvent>
#include <QEvent>
#include <QWheelEvent>
#include <QPushButton>
#include <QFileInfo>
#include <QFont>
#include <QLabel>
#include <QGuiApplication>
#include <QScreen>
#include <QSettings>
#include <QStatusBar>
#include <QWindow>
#include "common_types.h"
#include "image_viewer_window.h"

namespace
{
std::pair<QImage, QString> load_image_file(const QString& path)
{
    QImageReader reader(path);
    reader.setAutoTransform(true);

    QSize img_size = reader.size();

    if (!img_size.isValid())
    {
        if (reader.error() != QImageReader::UnknownError)
        {
            return {QImage(), reader.errorString()};
        }
    }

    if (img_size.isValid())
    {
        double estimated_mb = (static_cast<double>(img_size.width()) * img_size.height() * 4) / (1024.0 * 1024.0);

        if (estimated_mb > kMaxImageAllocMB)
        {
            double scale_factor = std::sqrt(kMaxImageAllocMB / estimated_mb);
            QSize safe_size = img_size * scale_factor;
            reader.setScaledSize(safe_size);
        }
    }

    QImage image = reader.read();

    if (image.isNull())
    {
        QString err = reader.errorString();
        if (err.isEmpty())
        {
            err = "Unknown error (Format not supported or file corrupted)";
        }
        return {QImage(), err};
    }

    return {image, QString()};
}
}

image_viewer_window::image_viewer_window(QWidget* parent)
    : QMainWindow(parent), current_index_(-1), view_(nullptr), scene_(nullptr), image_item_(nullptr), btn_prev_(nullptr), btn_next_(nullptr)
{
    setup_ui();
    image_cache_.setMaxCost(kMaxImageAllocMB * 1024 * 1024);
    load_settings();
}

image_viewer_window::~image_viewer_window()
{
    if (image_watcher_ != nullptr)
    {
        image_watcher_->disconnect();
        if (image_watcher_->isRunning())
        {
            image_watcher_->waitForFinished();
        }
    }

    if (preload_watcher_ != nullptr)
    {
        preload_watcher_->disconnect();
        if (preload_watcher_->isRunning())
        {
            preload_watcher_->waitForFinished();
        }
        delete preload_watcher_;
        preload_watcher_ = nullptr;
    }
}

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
    view_->setResizeAnchor(QGraphicsView::AnchorViewCenter);

    view_->setBackgroundBrush(Qt::black);

    view_->viewport()->installEventFilter(this);
    view_->installEventFilter(this);

    setCentralWidget(view_);

    image_info_label_ = new QLabel(this);
    statusBar()->addWidget(image_info_label_);

    zoom_label_ = new QLabel(this);
    statusBar()->addPermanentWidget(zoom_label_);

    auto* toolbar = addToolBar("View");
    toolbar->setMovable(false);

    view_mode_group_ = new QActionGroup(this);
    view_mode_group_->setExclusive(true);

    fit_window_action_ = toolbar->addAction("适应窗口");
    fit_window_action_->setCheckable(true);
    fit_window_action_->setShortcut(QKeySequence("Ctrl+0"));
    view_mode_group_->addAction(fit_window_action_);
    connect(fit_window_action_, &QAction::triggered, this, &image_viewer_window::set_fit_window_mode);

    actual_size_action_ = toolbar->addAction("1:1");
    actual_size_action_->setCheckable(true);
    actual_size_action_->setShortcut(QKeySequence("Ctrl+1"));
    view_mode_group_->addAction(actual_size_action_);
    connect(actual_size_action_, &QAction::triggered, this, &image_viewer_window::set_actual_size_mode);

    fit_width_action_ = toolbar->addAction("适应宽度");
    fit_width_action_->setCheckable(true);
    fit_width_action_->setShortcut(QKeySequence("Ctrl+2"));
    view_mode_group_->addAction(fit_width_action_);
    connect(fit_width_action_, &QAction::triggered, this, &image_viewer_window::set_fit_width_mode);

    full_screen_action_ = toolbar->addAction("全屏");
    full_screen_action_->setCheckable(true);
    full_screen_action_->setShortcut(QKeySequence(Qt::Key_F11));
    connect(full_screen_action_, &QAction::triggered, this, &image_viewer_window::toggle_full_screen);

    addAction(fit_window_action_);
    addAction(actual_size_action_);
    addAction(fit_width_action_);
    addAction(full_screen_action_);
    update_view_mode_actions();

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
    update_navigation_buttons();
}

void image_viewer_window::set_image_path(const QString& path)
{
    current_path_ = path;
    update_index_from_path();
    update_navigation_buttons();

    if (image_watcher_ == nullptr)
    {
        image_watcher_ = new QFutureWatcher<std::pair<QImage, QString>>(this);
    }

    scene_->clear();
    image_item_ = nullptr;
    setWindowTitle(QString("Loading %1...").arg(QFileInfo(path).fileName()));

    pending_preload_paths_.clear();

    if (image_cache_.contains(path))
    {
        display_image(*image_cache_.object(path), path);
        return;
    }

    disconnect(image_watcher_, &QFutureWatcher<std::pair<QImage, QString>>::finished, this, nullptr);

    connect(image_watcher_,
            &QFutureWatcher<std::pair<QImage, QString>>::finished,
            this,
            [this, path]()
            {
                if (current_path_ != path)
                {
                    return;
                }

                auto result = image_watcher_->result();
                QImage image = result.first;
                QString error_msg = result.second;

                if (image.isNull())
                {
                    setWindowTitle("Error loading image");

                    scene_->clear();
                    QGraphicsTextItem* text_item = scene_->addText(QString("Failed to load image:\n%1").arg(error_msg));

                    text_item->setDefaultTextColor(Qt::red);
                    QFont font = text_item->font();
                    font.setPointSize(16);
                    font.setBold(true);
                    text_item->setFont(font);

                    QRectF text_rect = text_item->boundingRect();
                    text_item->setPos(-text_rect.width() / 2, -text_rect.height() / 2);

                    return;
                }
                image_cache_.insert(path, new QImage(image), image.sizeInBytes());
                display_image(image, path);
            });

    image_watcher_->setFuture(QtConcurrent::run(load_image_file, path));
}

void image_viewer_window::set_image_list(const std::vector<QString>& paths)
{
    image_list_ = paths;
    update_index_from_path();
    update_navigation_buttons();
}

void image_viewer_window::load_settings()
{
    QSettings settings("gyl30", "ImageViewer");
    if (!restoreGeometry(settings.value("viewer_window/geometry").toByteArray()))
    {
        resize(1200, 800);
    }
}

void image_viewer_window::save_settings() const
{
    QSettings settings("gyl30", "ImageViewer");
    settings.setValue("viewer_window/geometry", saveGeometry());
}

void image_viewer_window::display_image(const QImage& image, const QString& path)
{
    QPixmap pixmap = QPixmap::fromImage(image);
    image_item_ = scene_->addPixmap(pixmap);
    scene_->setSceneRect(pixmap.rect());
    update_image_status(path, pixmap.size());

    if (image_item_ != nullptr)
    {
        has_manual_zoom_ = false;
        resize_window_to_image(pixmap.size());
        apply_auto_view();
    }

    setWindowTitle(QString("Viewer - %1 (%2x%3) [%4/%5]")
                       .arg(QFileInfo(path).fileName())
                       .arg(pixmap.width())
                       .arg(pixmap.height())
                       .arg(current_index_ + 1)
                       .arg(image_list_.size()));

    queue_adjacent_preloads();
}

void image_viewer_window::update_image_status(const QString& path, const QSize& image_size)
{
    QFileInfo file_info(path);
    current_image_size_ = image_size;
    current_file_size_ = file_info.size();

    QImageReader reader(path);
    current_image_format_ = QString::fromLatin1(reader.format()).toUpper();
    if (current_image_format_.isEmpty())
    {
        current_image_format_ = file_info.suffix().toUpper();
    }

    QString size_str;
    if (current_file_size_ < 1024)
    {
        size_str = QString("%1 B").arg(current_file_size_);
    }
    else if (current_file_size_ < static_cast<qint64>(1024 * 1024))
    {
        size_str = QString("%1 KB").arg(static_cast<double>(current_file_size_) / 1024.0, 0, 'f', 1);
    }
    else
    {
        size_str = QString("%1 MB").arg(static_cast<double>(current_file_size_) / (1024.0 * 1024.0), 0, 'f', 2);
    }

    image_info_label_->setText(QString("%1 | %2 | %3x%4 | %5")
                                   .arg(file_info.fileName())
                                   .arg(current_image_format_)
                                   .arg(current_image_size_.width())
                                   .arg(current_image_size_.height())
                                   .arg(size_str));
}

void image_viewer_window::update_zoom_status()
{
    const qreal scale = view_->transform().m11();
    zoom_label_->setText(QString("%1%").arg(scale * 100.0, 0, 'f', 0));
}

void image_viewer_window::queue_adjacent_preloads()
{
    pending_preload_paths_.clear();

    if (current_index_ < 0 || image_list_.empty())
    {
        return;
    }

    const std::array<ptrdiff_t, 2> offsets = {1, -1};
    for (ptrdiff_t offset : offsets)
    {
        ptrdiff_t idx = current_index_ + offset;
        if (idx < 0 || idx >= static_cast<ptrdiff_t>(image_list_.size()))
        {
            continue;
        }

        const QString& path = image_list_[idx];
        if (image_cache_.contains(path))
        {
            continue;
        }
        pending_preload_paths_.append(path);
    }

    if (preload_watcher_ == nullptr)
    {
        preload_watcher_ = new QFutureWatcher<std::pair<QString, QImage>>(this);
        connect(preload_watcher_,
                &QFutureWatcher<std::pair<QString, QImage>>::finished,
                this,
                [this]()
                {
                    const auto result = preload_watcher_->result();
                    if (!result.second.isNull())
                    {
                        image_cache_.insert(result.first, new QImage(result.second), result.second.sizeInBytes());
                    }
                    start_next_preload();
                });
    }

    start_next_preload();
}

void image_viewer_window::start_next_preload()
{
    if (preload_watcher_ == nullptr || preload_watcher_->isRunning())
    {
        return;
    }

    while (!pending_preload_paths_.isEmpty())
    {
        QString preload_path = pending_preload_paths_.takeFirst();
        if (image_cache_.contains(preload_path))
        {
            continue;
        }

        preload_watcher_->setFuture(QtConcurrent::run(
            [preload_path]()
            {
                auto result = load_image_file(preload_path);
                return std::make_pair(preload_path, result.first);
            }));
        return;
    }
}

void image_viewer_window::update_view_mode_actions()
{
    fit_window_action_->setChecked(current_view_mode_ == view_mode::fit_window);
    actual_size_action_->setChecked(current_view_mode_ == view_mode::actual_size);
    fit_width_action_->setChecked(current_view_mode_ == view_mode::fit_width);
    full_screen_action_->setChecked(isFullScreen());
}

void image_viewer_window::resize_window_to_image(const QSize& image_size)
{
    if (!image_size.isValid() || isMaximized() || isFullScreen())
    {
        return;
    }

    QScreen* current_screen = screen();
    if (current_screen == nullptr && windowHandle() != nullptr)
    {
        current_screen = windowHandle()->screen();
    }
    if (current_screen == nullptr)
    {
        current_screen = QGuiApplication::primaryScreen();
    }
    if (current_screen == nullptr)
    {
        return;
    }

    const QSize frame_padding = frameGeometry().size() - size();
    const QSize viewport_size = view_->viewport()->size();
    const QSize viewport_padding = size() - viewport_size;
    const QRect available_geometry = current_screen->availableGeometry().adjusted(40, 40, -40, -40);
    const QSize max_window_size =
        (available_geometry.size() - frame_padding).expandedTo(QSize(0, 0));
    const QSize target_window_size =
        QSize(image_size.width() + viewport_padding.width(), image_size.height() + viewport_padding.height())
            .boundedTo(max_window_size);

    resize(target_window_size);
}

void image_viewer_window::apply_auto_view()
{
    if (image_item_ == nullptr)
    {
        return;
    }

    view_->resetTransform();

    const QSize viewport_size = view_->viewport()->size();
    const QRectF image_rect = image_item_->boundingRect();

    if (current_view_mode_ == view_mode::actual_size)
    {
        view_->centerOn(image_item_);
        update_zoom_status();
        return;
    }

    if (current_view_mode_ == view_mode::fit_width)
    {
        if (image_rect.width() > viewport_size.width() && image_rect.width() > 0.0)
        {
            qreal scale = static_cast<qreal>(viewport_size.width()) / image_rect.width();
            view_->scale(scale, scale);
        }
        view_->centerOn(image_item_);
        update_zoom_status();
        return;
    }

    if (image_rect.width() > viewport_size.width() || image_rect.height() > viewport_size.height())
    {
        view_->fitInView(image_item_, Qt::KeepAspectRatio);
        update_zoom_status();
        return;
    }

    view_->centerOn(image_item_);
    update_zoom_status();
}

void image_viewer_window::update_index_from_path()
{
    if (image_list_.empty())
    {
        current_index_ = -1;
        return;
    }

    auto it = std::find(image_list_.begin(), image_list_.end(), current_path_);
    if (it != image_list_.end())
    {
        current_index_ = std::distance(image_list_.begin(), it);
    }
    else
    {
        current_index_ = -1;
    }
}

void image_viewer_window::showEvent(QShowEvent* event) { QMainWindow::showEvent(event); }

void image_viewer_window::resizeEvent(QResizeEvent* event)
{
    QMainWindow::resizeEvent(event);

    if (image_item_ != nullptr && !has_manual_zoom_)
    {
        apply_auto_view();
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
    if (event->type() == QEvent::KeyPress)
    {
        auto* key_event = static_cast<QKeyEvent*>(event);
        if (key_event->key() == Qt::Key_Left)
        {
            load_prev_image();
            return true;
        }
        if (key_event->key() == Qt::Key_Right)
        {
            load_next_image();
            return true;
        }
    }

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

void image_viewer_window::zoom_in()
{
    has_manual_zoom_ = true;
    view_->scale(1.2, 1.2);
    update_zoom_status();
}

void image_viewer_window::zoom_out()
{
    has_manual_zoom_ = true;
    view_->scale(1.0 / 1.2, 1.0 / 1.2);
    update_zoom_status();
}

void image_viewer_window::load_prev_image() { navigate_image(-1); }

void image_viewer_window::load_next_image() { navigate_image(1); }

void image_viewer_window::set_fit_window_mode()
{
    current_view_mode_ = view_mode::fit_window;
    has_manual_zoom_ = false;
    update_view_mode_actions();
    apply_auto_view();
}

void image_viewer_window::set_actual_size_mode()
{
    current_view_mode_ = view_mode::actual_size;
    has_manual_zoom_ = false;
    update_view_mode_actions();
    apply_auto_view();
}

void image_viewer_window::set_fit_width_mode()
{
    current_view_mode_ = view_mode::fit_width;
    has_manual_zoom_ = false;
    update_view_mode_actions();
    apply_auto_view();
}

void image_viewer_window::toggle_full_screen()
{
    if (isFullScreen())
    {
        showNormal();
    }
    else
    {
        showFullScreen();
    }
    update_view_mode_actions();
}

void image_viewer_window::navigate_image(int delta)
{
    if (image_list_.empty())
    {
        return;
    }

    if (current_index_ == -1)
    {
        update_index_from_path();
        if (current_index_ == -1)
        {
            return;
        }
    }

    ptrdiff_t new_idx = current_index_ + delta;

    if (new_idx < 0)
    {
        return;
    }

    if (new_idx >= static_cast<ptrdiff_t>(image_list_.size()))
    {
        return;
    }

    set_image_path(image_list_[new_idx]);
}

void image_viewer_window::update_navigation_buttons()
{
    bool has_prev = current_index_ > 0;
    bool has_next = current_index_ >= 0 && current_index_ < static_cast<ptrdiff_t>(image_list_.size()) - 1;

    btn_prev_->setEnabled(has_prev);
    btn_next_->setEnabled(has_next);
}

void image_viewer_window::load_image(const QString& path) { set_image_path(path); }

void image_viewer_window::keyPressEvent(QKeyEvent* event)
{
    if (event->key() == Qt::Key_Left)
    {
        load_prev_image();
    }
    else if (event->key() == Qt::Key_Right)
    {
        load_next_image();
    }
    else if (event->key() == Qt::Key_Escape)
    {
        close();
    }
    else
    {
        QMainWindow::keyPressEvent(event);
    }
}

void image_viewer_window::closeEvent(QCloseEvent* event)
{
    save_settings();

    if (image_watcher_ != nullptr)
    {
        image_watcher_->disconnect();
        if (image_watcher_->isRunning())
        {
            image_watcher_->waitForFinished();
        }
    }

    if (preload_watcher_ != nullptr)
    {
        preload_watcher_->disconnect();
        if (preload_watcher_->isRunning())
        {
            preload_watcher_->waitForFinished();
        }
    }

    scene_->clear();
    image_item_ = nullptr;
    current_path_.clear();
    image_info_label_->clear();
    zoom_label_->clear();
    has_manual_zoom_ = false;
    update_navigation_buttons();
    update_view_mode_actions();
    QMainWindow::closeEvent(event);
}
