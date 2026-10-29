#include <cmath>
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
#include <QToolButton>
#include <QMenu>
#include <QAction>
#include <QActionGroup>
#include <QResizeEvent>
#include <QEvent>
#include <QWheelEvent>
#include <QApplication>
#include <QFileInfo>
#include <QDateTime>
#include <QFont>
#include <QLabel>
#include <QMouseEvent>
#include <QInputDialog>
#include <QMovie>
#include <QSettings>
#include <QStatusBar>
#include <QTimer>
#include "common_types.h"
#include "image_viewer_window.h"
#include "window_title_bar.h"
#include "shortcut_dialog.h"

namespace
{
bool is_animated_image(const QString& path)
{
    QImageReader reader(path);
    return reader.supportsAnimation() && reader.imageCount() != 1;
}

std::pair<QImage, QString> load_image_file(const QString& path)
{
    QImageReader reader(path);
    reader.setAutoTransform(true);

    const QSize img_size = reader.size();
    const bool supports_scaled_size = reader.supportsOption(QImageIOHandler::ScaledSize);

    if (!img_size.isValid())
    {
        if (reader.error() != QImageReader::UnknownError)
        {
            return {QImage(), reader.errorString()};
        }
    }

    if (img_size.isValid())
    {
        const double estimated_mb = (static_cast<double>(img_size.width()) * img_size.height() * 4) / (1024.0 * 1024.0);

        if (estimated_mb > kMaxImageAllocMB && supports_scaled_size)
        {
            const double scale_factor = std::sqrt(kMaxImageAllocMB / estimated_mb);
            QSize safe_size = (img_size * scale_factor).expandedTo(QSize(1, 1));
            const QByteArray format = reader.format();
            constexpr int kJpegScaleDenominator = 8;
            if ((format == "jpeg" || format == "jpg") && scale_factor >= 1.0 / kJpegScaleDenominator)
            {
                // 对齐 JPEG 原生缩放，避免先分配完整图再分配缩放结果。
                const int numerator = static_cast<int>(scale_factor * kJpegScaleDenominator);
                safe_size = QSize(img_size.width() * numerator / kJpegScaleDenominator,
                                  img_size.height() * numerator / kJpegScaleDenominator).expandedTo(QSize(1, 1));
            }
            reader.setScaledSize(safe_size);
        }
        else if (estimated_mb > kQtImageReaderAllocMB && !supports_scaled_size)
        {
            return {QImage(),
                    QString("图片过大，无法安全解码（约 %1 MB，超过 %2 MB），且当前格式不支持缩小读取。")
                        .arg(estimated_mb, 0, 'f', 1)
                        .arg(kQtImageReaderAllocMB)};
        }
    }

    QImage image = reader.read();

    if (image.isNull())
    {
        QString err = reader.errorString();
        if (err.isEmpty())
        {
            err = "文件格式不受支持或文件已损坏";
        }
        return {QImage(), QString("无法读取图片数据：%1").arg(err)};
    }

    return {image, QString()};
}
}

image_viewer_window::image_viewer_window(QWidget* parent)
    : QMainWindow(parent), current_index_(-1), view_(nullptr), scene_(nullptr), image_item_(nullptr), prev_action_(nullptr), next_action_(nullptr)
{
    setup_ui();
    load_settings();
}

image_viewer_window::~image_viewer_window()
{
    clear_movie();

    if (image_watcher_ != nullptr)
    {
        image_watcher_->disconnect();
        if (image_watcher_->isRunning())
        {
            image_watcher_->waitForFinished();
        }
    }

}

void image_viewer_window::setup_ui()
{
    scene_ = new QGraphicsScene(this);
    view_ = new QGraphicsView(this);
    view_->setFrameShape(QFrame::NoFrame);
    view_->setScene(scene_);
    view_->setDragMode(QGraphicsView::ScrollHandDrag);
    view_->setRenderHint(QPainter::Antialiasing);
    view_->setRenderHint(QPainter::SmoothPixmapTransform);
    view_->setViewportUpdateMode(QGraphicsView::SmartViewportUpdate);

    view_->setTransformationAnchor(QGraphicsView::AnchorUnderMouse);
    view_->setResizeAnchor(QGraphicsView::AnchorViewCenter);

    view_->setBackgroundBrush(QApplication::palette().base());

    view_->viewport()->installEventFilter(this);
    view_->installEventFilter(this);

    setCentralWidget(view_);

    image_info_label_ = new QLabel(this);
    statusBar()->addWidget(image_info_label_);

    zoom_label_ = new QLabel(this);
    statusBar()->addPermanentWidget(zoom_label_);

    auto* toolbar = new QToolBar("预览", this);
    toolbar->setObjectName("preview_toolbar");
    toolbar->setMovable(false);
    toolbar->setFloatable(false);
    auto* back_action = toolbar->addAction(toolbar_icon(":/icons/grid.svg"), "图库");
    back_action->setToolTip("返回图库（Esc）");
    connect(back_action, &QAction::triggered, this, &QWidget::close);
    toolbar->addSeparator();
    prev_action_ = toolbar->addAction(toolbar_icon(":/icons/previous.svg"), "上一张");
    position_label_ = new QLabel(this);
    position_label_->setMargin(4);
    position_label_->setAlignment(Qt::AlignCenter);
    toolbar->addWidget(position_label_);
    next_action_ = toolbar->addAction(toolbar_icon(":/icons/next.svg"), "下一张");
    prev_action_->setToolTip("上一张（H / Left / Backspace / 鼠标后退键）");
    next_action_->setToolTip("下一张（L / Right / Space / 鼠标前进键）");
    connect(prev_action_, &QAction::triggered, this, &image_viewer_window::load_prev_image);
    connect(next_action_, &QAction::triggered, this, &image_viewer_window::load_next_image);
    toolbar->addSeparator();

    view_mode_group_ = new QActionGroup(this);
    view_mode_group_->setExclusive(true);
    auto* mode_button = new QToolButton(toolbar);
    mode_button->setObjectName("preview_mode");
    mode_button->setText("适应窗口");
    mode_button->setIcon(toolbar_icon(":/icons/fit.svg"));
    mode_button->setToolButtonStyle(Qt::ToolButtonIconOnly);
    mode_button->setToolTip("适应窗口（Ctrl+0）");
    mode_button->setPopupMode(QToolButton::InstantPopup);
    auto* mode_menu = new QMenu(mode_button);
    mode_button->setMenu(mode_menu);
    toolbar->addWidget(mode_button);

    fit_window_action_ = mode_menu->addAction("适应窗口");
    fit_window_action_->setIcon(toolbar_icon(":/icons/fit.svg"));
    fit_window_action_->setCheckable(true);
    fit_window_action_->setShortcut(QKeySequence("Ctrl+0"));
    view_mode_group_->addAction(fit_window_action_);
    connect(fit_window_action_, &QAction::triggered, this, &image_viewer_window::set_fit_window_mode);

    actual_size_action_ = mode_menu->addAction("1:1");
    actual_size_action_->setIcon(toolbar_icon(":/icons/actual-size.svg"));
    actual_size_action_->setCheckable(true);
    actual_size_action_->setShortcut(QKeySequence("Ctrl+1"));
    view_mode_group_->addAction(actual_size_action_);
    connect(actual_size_action_, &QAction::triggered, this, &image_viewer_window::set_actual_size_mode);

    fit_width_action_ = mode_menu->addAction("适应宽度");
    fit_width_action_->setIcon(toolbar_icon(":/icons/fit-width.svg"));
    fit_width_action_->setCheckable(true);
    fit_width_action_->setShortcut(QKeySequence("Ctrl+2"));
    view_mode_group_->addAction(fit_width_action_);
    connect(fit_width_action_, &QAction::triggered, this, &image_viewer_window::set_fit_width_mode);

    for (auto* action : {fit_window_action_, actual_size_action_, fit_width_action_})
    {
        connect(action, &QAction::toggled, mode_button, [mode_button, action](bool checked)
        {
            if (checked)
            {
                mode_button->setText(action->text());
                mode_button->setIcon(action->icon());
                mode_button->setToolTip(QString("%1（%2）").arg(action->text(), action->shortcut().toString(QKeySequence::NativeText)));
            }
        });
    }
    full_screen_action_ = new QAction("全屏", this);
    full_screen_action_->setCheckable(true);
    full_screen_action_->setShortcut(QKeySequence(Qt::Key_F11));
    connect(full_screen_action_, &QAction::triggered, this, &image_viewer_window::toggle_full_screen);

    auto* zoom_out_action = toolbar->addAction(toolbar_icon(":/icons/zoom-out.svg"), "缩小");
    auto* zoom_in_action = toolbar->addAction(toolbar_icon(":/icons/zoom-in.svg"), "放大");
    zoom_out_action->setShortcut(QKeySequence::ZoomOut);
    zoom_in_action->setShortcut(QKeySequence::ZoomIn);
    connect(zoom_out_action, &QAction::triggered, this, &image_viewer_window::zoom_out);
    connect(zoom_in_action, &QAction::triggered, this, &image_viewer_window::zoom_in);
    rotate_left_action_ = new QAction("左转", this);
    rotate_left_action_->setShortcut(QKeySequence("Ctrl+L"));
    connect(rotate_left_action_, &QAction::triggered, this, &image_viewer_window::rotate_left);

    rotate_right_action_ = toolbar->addAction(toolbar_icon(":/icons/rotate-right.svg"), "右转");
    rotate_right_action_->setShortcut(QKeySequence("Ctrl+R"));
    connect(rotate_right_action_, &QAction::triggered, this, &image_viewer_window::rotate_right);

    flip_horizontal_action_ = new QAction("水平翻转", this);
    flip_horizontal_action_->setShortcut(QKeySequence("Ctrl+H"));
    connect(flip_horizontal_action_, &QAction::triggered, this, &image_viewer_window::flip_horizontal);

    flip_vertical_action_ = new QAction("垂直翻转", this);
    flip_vertical_action_->setShortcut(QKeySequence("Ctrl+Shift+H"));
    connect(flip_vertical_action_, &QAction::triggered, this, &image_viewer_window::flip_vertical);

    slideshow_action_ = toolbar->addAction(toolbar_icon(":/icons/play.svg"), "幻灯片");
    slideshow_action_->setCheckable(true);
    slideshow_action_->setShortcut(QKeySequence(Qt::Key_F5));
    slideshow_action_->setToolTip("播放幻灯片（F5）");
    connect(slideshow_action_, &QAction::toggled, this, [this](bool playing)
    {
        slideshow_action_->setIcon(toolbar_icon(playing ? ":/icons/pause.svg" : ":/icons/play.svg"));
        slideshow_action_->setToolTip(playing ? "暂停幻灯片（F5）" : "播放幻灯片（F5）");
    });
    connect(slideshow_action_, &QAction::triggered, this, &image_viewer_window::toggle_slideshow);

    auto* spacer = new QWidget(toolbar);
    spacer->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    spacer->setAttribute(Qt::WA_TransparentForMouseEvents);
    toolbar->addWidget(spacer);
    auto* shortcuts_action = new QAction("?", this);
    shortcuts_action->setObjectName("show_shortcuts");
    shortcuts_action->setShortcut(QKeySequence(Qt::Key_F1));
    shortcuts_action->setToolTip(QString());
    toolbar->addAction(shortcuts_action);
    auto* shortcuts_button = qobject_cast<QToolButton*>(toolbar->widgetForAction(shortcuts_action));
    shortcuts_button->setObjectName("show_shortcuts");
    shortcuts_button->setToolButtonStyle(Qt::ToolButtonTextOnly);
    shortcuts_button->setAccessibleName("快捷键");
    shortcuts_button->setToolTip(QString());
    auto* more_button = new QToolButton(toolbar);
    more_button->setObjectName("more_menu");
    more_button->setText("更多");
    more_button->setIcon(toolbar_icon(":/icons/more.svg"));
    more_button->setToolButtonStyle(Qt::ToolButtonIconOnly);
    more_button->setToolTip("更多");
    more_button->setAccessibleName("更多");
    more_button->setPopupMode(QToolButton::InstantPopup);
    auto* more_menu = new QMenu(more_button);
    more_button->setMenu(more_menu);
    toolbar->addWidget(more_button);
    more_menu->addAction(rotate_left_action_);
    more_menu->addAction(flip_horizontal_action_);
    more_menu->addAction(flip_vertical_action_);
    more_menu->addSeparator();

    slideshow_loop_action_ = more_menu->addAction("循环播放");
    slideshow_loop_action_->setCheckable(true);
    connect(slideshow_loop_action_, &QAction::triggered, this, &image_viewer_window::toggle_slideshow_loop);

    slideshow_interval_action_ = more_menu->addAction("播放间隔");
    connect(slideshow_interval_action_, &QAction::triggered, this, &image_viewer_window::configure_slideshow_interval);

    more_menu->addSeparator();
    more_menu->addAction(full_screen_action_);
    connect(shortcuts_action,
            &QAction::triggered,
            this,
            [this]()
            {
                show_shortcuts_dialog(this,
                                      {{"预览", "上一张 / 下一张", "H / L 或 ← / →"},
                                       {"预览", "返回图库", "Q / Esc"},
                                       {"预览", "放大 / 缩小", "+ / - 或 Ctrl+滚轮"},
                                       {"预览", "适应窗口 / 1:1 / 适应宽度", "Ctrl+0 / 1 / 2"},
                                       {"预览", "上一张 / 下一张", "Backspace / Space"},
                                       {"预览", "首张 / 末张", "Home / End"},
                                       {"预览", "后退 / 前进翻图", "鼠标侧键"},
                                       {"预览", "移到回收站", "Delete"},
                                       {"预览", "左转 / 右转", "Ctrl+L / Ctrl+R"},
                                       {"预览", "水平 / 垂直翻转", "Ctrl+H / Ctrl+Shift+H"},
                                       {"预览", "幻灯片", "F5"},
                                       {"预览", "全屏", "F11 / F"},
                                       {"预览", "显示快捷键", "问号按钮 / F1"}});
            });

    addAction(fit_window_action_);
    addAction(actual_size_action_);
    addAction(fit_width_action_);
    addAction(full_screen_action_);
    addAction(rotate_left_action_);
    addAction(rotate_right_action_);
    addAction(flip_horizontal_action_);
    addAction(flip_vertical_action_);
    addAction(slideshow_action_);
    addAction(shortcuts_action);
    update_view_mode_actions();

    slideshow_timer_ = new QTimer(this);
    slideshow_timer_->setInterval(slideshow_interval_ms_);
    connect(slideshow_timer_, &QTimer::timeout, this, &image_viewer_window::advance_slideshow);

    update_navigation_buttons();
    setMenuWidget(new window_title_bar(this, toolbar));
}

void image_viewer_window::set_image_path(const QString& path)
{
    current_path_ = path;
    has_manual_zoom_ = false;
    image_info_label_->clear();
    zoom_label_->clear();
    update_index_from_path();
    update_navigation_buttons();
    emit current_image_changed(path);
    clear_movie();

    if (image_watcher_ == nullptr)
    {
        image_watcher_ = new QFutureWatcher<std::pair<QImage, QString>>(this);
    }

    scene_->clear();
    image_item_ = nullptr;
    setWindowTitle(QString("正在打开 %1…").arg(QFileInfo(path).fileName()));

    if (is_animated_image(path))
    {
        start_movie(path);
        return;
    }

    if (!loading_path_.isEmpty())
    {
        return;
    }
    load_image(path);
}

void image_viewer_window::load_image(const QString& path)
{
    loading_path_ = path;
    const QFileInfo source(path);
    const QDateTime modified = source.lastModified();
    const qint64 file_size = source.size();

    disconnect(image_watcher_, &QFutureWatcher<std::pair<QImage, QString>>::finished, this, nullptr);

    connect(image_watcher_,
            &QFutureWatcher<std::pair<QImage, QString>>::finished,
            this,
            [this, path, modified, file_size]()
            {
                loading_path_.clear();
                auto result = image_watcher_->future().takeResult();
                const QFileInfo current(path);
                if (current_path_ != path || current.lastModified() != modified || current.size() != file_size)
                {
                    result.first = QImage();
                    if (!current_path_.isEmpty() && image_item_ == nullptr && movie_ == nullptr)
                    {
                        load_image(current_path_);
                    }
                    return;
                }

                QImage image = std::move(result.first);
                QString error_msg = std::move(result.second);

                if (image.isNull())
                {
                    setWindowTitle("加载图片失败");

                    scene_->clear();
                    QGraphicsTextItem* text_item = scene_->addText(QString("加载图片失败：\n%1").arg(error_msg));

                    text_item->setDefaultTextColor(Qt::red);
                    QFont font = text_item->font();
                    font.setPointSize(16);
                    font.setBold(true);
                    text_item->setFont(font);

                    QRectF text_rect = text_item->boundingRect();
                    text_item->setPos(-text_rect.width() / 2, -text_rect.height() / 2);
                    scene_->setSceneRect(text_item->sceneBoundingRect());
                    view_->resetTransform();
                    view_->centerOn(text_item);

                    return;
                }
                display_image(std::move(image), path);
            });

    image_watcher_->setFuture(QtConcurrent::run(load_image_file, path));
}

void image_viewer_window::set_image_list(const QStringList& paths)
{
    image_list_ = paths;
    update_index_from_path();
    update_navigation_buttons();
}

void image_viewer_window::remove_image_path(const QString& path)
{
    auto it = std::find(image_list_.begin(), image_list_.end(), path);
    if (it == image_list_.end())
    {
        return;
    }

    const ptrdiff_t removed_index = std::distance(image_list_.begin(), it);
    const bool removing_current = (current_path_ == path);

    image_list_.erase(it);

    if (image_list_.empty())
    {
        close();
        return;
    }

    if (!removing_current)
    {
        update_index_from_path();
        update_navigation_buttons();
        return;
    }

    ptrdiff_t next_index = std::min(removed_index, static_cast<ptrdiff_t>(image_list_.size()) - 1);
    set_image_path(image_list_[next_index]);
}

void image_viewer_window::load_settings()
{
    QSettings settings("gyl30", "ImageViewer");
    if (!restoreGeometry(settings.value("viewer_window/geometry").toByteArray()))
    {
        resize(1200, 800);
    }
    const int saved_view_mode = settings.value("viewer_window/view_mode", static_cast<int>(view_mode::fit_window)).toInt();
    if (saved_view_mode == static_cast<int>(view_mode::actual_size))
    {
        current_view_mode_ = view_mode::actual_size;
    }
    else if (saved_view_mode == static_cast<int>(view_mode::fit_width))
    {
        current_view_mode_ = view_mode::fit_width;
    }
    else
    {
        current_view_mode_ = view_mode::fit_window;
    }
    slideshow_interval_ms_ = settings.value("viewer_window/slideshow_interval_ms", 3000).toInt();
    slideshow_loop_enabled_ = settings.value("viewer_window/slideshow_loop_enabled", false).toBool();
    update_view_mode_actions();
    if (slideshow_loop_action_ != nullptr)
    {
        slideshow_loop_action_->setChecked(slideshow_loop_enabled_);
    }
    if (slideshow_timer_ != nullptr)
    {
        slideshow_timer_->setInterval(slideshow_interval_ms_);
    }
}

void image_viewer_window::save_settings() const
{
    QSettings settings("gyl30", "ImageViewer");
    settings.setValue("viewer_window/geometry", saveGeometry());
    settings.setValue("viewer_window/view_mode", static_cast<int>(current_view_mode_));
    settings.setValue("viewer_window/slideshow_interval_ms", slideshow_interval_ms_);
    settings.setValue("viewer_window/slideshow_loop_enabled", slideshow_loop_enabled_);
}

void image_viewer_window::display_image(QImage image, const QString& path)
{
    QPixmap pixmap = QPixmap::fromImage(std::move(image));
    image_item_ = scene_->addPixmap(pixmap);
    scene_->setSceneRect(pixmap.rect());
    rotation_degrees_ = 0;
    flip_horizontal_ = false;
    flip_vertical_ = false;
    apply_image_transform();
    update_image_status(path, pixmap.size());

    if (image_item_ != nullptr)
    {
        has_manual_zoom_ = false;
        apply_auto_view();
    }

    setWindowTitle(QString("Viewer - %1 (%2x%3) [%4/%5]")
                       .arg(QFileInfo(path).fileName())
                       .arg(pixmap.width())
                       .arg(pixmap.height())
                       .arg(current_index_ + 1)
                       .arg(image_list_.size()));

}

void image_viewer_window::update_image_status(const QString& path, const QSize& image_size)
{
    QFileInfo file_info(path);
    const qint64 file_size = file_info.size();

    QImageReader reader(path);
    const QSize source_size = reader.size();
    QString image_format = QString::fromLatin1(reader.format()).toUpper();
    if (image_format.isEmpty())
    {
        image_format = file_info.suffix().toUpper();
    }

    QString size_str;
    if (file_size < 1024)
    {
        size_str = QString("%1 B").arg(file_size);
    }
    else if (file_size < static_cast<qint64>(1024 * 1024))
    {
        size_str = QString("%1 KB").arg(static_cast<double>(file_size) / 1024.0, 0, 'f', 1);
    }
    else
    {
        size_str = QString("%1 MB").arg(static_cast<double>(file_size) / (1024.0 * 1024.0), 0, 'f', 2);
    }

    const auto read_text =
        [&reader](std::initializer_list<const char*> keys)
    {
        for (const char* key : keys)
        {
            const QString value = reader.text(QString::fromLatin1(key)).trimmed();
            if (!value.isEmpty())
            {
                return value;
            }
        }
        return QString();
    };

    const QString capture_time = read_text({"DateTimeOriginal", "DateTimeDigitized", "DateTime"});
    const QString camera_make = read_text({"Make"});
    const QString camera_model = read_text({"Model", "CameraModelName"});

    QStringList info_parts = {
        file_info.fileName(),
        image_format,
        QString("%1x%2").arg(image_size.width()).arg(image_size.height()),
        size_str};

    if (!capture_time.isEmpty())
    {
        info_parts.append(capture_time);
    }

    if (!camera_model.isEmpty())
    {
        if (!camera_make.isEmpty() && !camera_model.startsWith(camera_make, Qt::CaseInsensitive))
        {
            info_parts.append(QString("%1 %2").arg(camera_make, camera_model));
        }
        else
        {
            info_parts.append(camera_model);
        }
    }
    else if (!camera_make.isEmpty())
    {
        info_parts.append(camera_make);
    }

    if (source_size.isValid() && source_size != image_size)
    {
        const double estimated_mb =
            (static_cast<double>(source_size.width()) * source_size.height() * 4.0) / (1024.0 * 1024.0);
        if (estimated_mb > kMaxImageAllocMB)
        {
            info_parts.append(
                QString("原图 %1x%2，已按内存限制缩小加载")
                    .arg(source_size.width())
                    .arg(source_size.height()));
        }
    }

    image_info_label_->setText(info_parts.join(" | "));
}

void image_viewer_window::update_zoom_status()
{
    const qreal scale = view_->transform().m11();
    zoom_label_->setText(QString("%1%").arg(scale * 100.0, 0, 'f', 0));
}

void image_viewer_window::apply_image_transform()
{
    if (image_item_ == nullptr)
    {
        return;
    }

    image_item_->setTransformOriginPoint(image_item_->boundingRect().center());

    QTransform transform;
    transform.scale(flip_horizontal_ ? -1.0 : 1.0, flip_vertical_ ? -1.0 : 1.0);
    transform.rotate(rotation_degrees_);
    image_item_->setTransform(transform);
    scene_->setSceneRect(image_item_->sceneBoundingRect());

    if (has_manual_zoom_)
    {
        view_->centerOn(image_item_);
        update_zoom_status();
        return;
    }

    apply_auto_view();
}

void image_viewer_window::clear_movie()
{
    if (movie_ == nullptr)
    {
        return;
    }

    movie_->stop();
    movie_->deleteLater();
    movie_ = nullptr;
}

void image_viewer_window::start_movie(const QString& path)
{
    movie_ = new QMovie(path, QByteArray(), this);
    movie_->setCacheMode(QMovie::CacheNone);

    if (!movie_->isValid())
    {
        clear_movie();
        setWindowTitle("无法读取动图");
        scene_->clear();
        image_item_ = nullptr;
        image_info_label_->setText("文件损坏或不支持动画解码");
        zoom_label_->clear();
        return;
    }

    connect(movie_,
            &QMovie::frameChanged,
            this,
            [this, path](int)
            {
                if (movie_ == nullptr || current_path_ != path)
                {
                    return;
                }

                QPixmap pixmap = movie_->currentPixmap();
                if (pixmap.isNull())
                {
                    return;
                }

                if (image_item_ == nullptr)
                {
                    image_item_ = scene_->addPixmap(pixmap);
                    rotation_degrees_ = 0;
                    flip_horizontal_ = false;
                    flip_vertical_ = false;
                    update_image_status(path, pixmap.size());
                    apply_image_transform();
                    setWindowTitle(QString("Viewer - %1 (%2x%3) [%4/%5]")
                                       .arg(QFileInfo(path).fileName())
                                       .arg(pixmap.width())
                                       .arg(pixmap.height())
                                       .arg(current_index_ + 1)
                                       .arg(image_list_.size()));
                    return;
                }

                image_item_->setPixmap(pixmap);
                scene_->setSceneRect(image_item_->sceneBoundingRect());
            });

    movie_->start();
}

void image_viewer_window::update_view_mode_actions()
{
    fit_window_action_->setChecked(current_view_mode_ == view_mode::fit_window);
    actual_size_action_->setChecked(current_view_mode_ == view_mode::actual_size);
    fit_width_action_->setChecked(current_view_mode_ == view_mode::fit_width);
    full_screen_action_->setChecked(isFullScreen());
}

void image_viewer_window::apply_auto_view()
{
    if (image_item_ == nullptr)
    {
        return;
    }

    view_->resetTransform();

    const QSize viewport_size = view_->viewport()->size();
    const QRectF image_rect = image_item_->sceneBoundingRect();

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

    auto it = std::find(image_list_.cbegin(), image_list_.cend(), current_path_);
    if (it != image_list_.cend())
    {
        current_index_ = std::distance(image_list_.cbegin(), it);
    }
    else
    {
        current_index_ = -1;
    }
}

void image_viewer_window::resizeEvent(QResizeEvent* event)
{
    QMainWindow::resizeEvent(event);

    if (image_item_ != nullptr && !has_manual_zoom_)
    {
        apply_auto_view();
    }

}

bool image_viewer_window::eventFilter(QObject* watched, QEvent* event)
{
    if (watched == view_->viewport() && event->type() == QEvent::MouseButtonPress)
    {
        const auto button = static_cast<QMouseEvent*>(event)->button();
        if (button == Qt::BackButton)
        {
            load_prev_image();
            return true;
        }
        if (button == Qt::ForwardButton)
        {
            load_next_image();
            return true;
        }
    }
    if (event->type() == QEvent::KeyPress)
    {
        auto* key_event = static_cast<QKeyEvent*>(event);
        const bool plain_key = key_event->modifiers() == Qt::NoModifier;
        if (plain_key && key_event->key() == Qt::Key_H)
        {
            load_prev_image();
            return true;
        }
        if (plain_key && key_event->key() == Qt::Key_L)
        {
            load_next_image();
            return true;
        }
        if (plain_key && key_event->key() == Qt::Key_Q)
        {
            close();
            return true;
        }
        if (plain_key && key_event->key() == Qt::Key_F)
        {
            toggle_full_screen();
            return true;
        }
        if (plain_key && key_event->key() == Qt::Key_0)
        {
            set_fit_window_mode();
            return true;
        }
        if ((key_event->key() == Qt::Key_Plus || key_event->key() == Qt::Key_Equal)
            && (key_event->modifiers() == Qt::NoModifier || key_event->modifiers() == Qt::ShiftModifier))
        {
            zoom_in();
            return true;
        }
        if (plain_key && key_event->key() == Qt::Key_Minus)
        {
            zoom_out();
            return true;
        }
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
        const QPoint delta = wheel_event->angleDelta().isNull() ? wheel_event->pixelDelta() : wheel_event->angleDelta();

        if ((wheel_event->modifiers() & Qt::ControlModifier) != 0U && delta.y() != 0)
        {
            if (delta.y() > 0)
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

void image_viewer_window::rotate_left()
{
    rotation_degrees_ = (rotation_degrees_ + 270) % 360;
    apply_image_transform();
}

void image_viewer_window::rotate_right()
{
    rotation_degrees_ = (rotation_degrees_ + 90) % 360;
    apply_image_transform();
}

void image_viewer_window::flip_horizontal()
{
    flip_horizontal_ = !flip_horizontal_;
    apply_image_transform();
}

void image_viewer_window::flip_vertical()
{
    flip_vertical_ = !flip_vertical_;
    apply_image_transform();
}

void image_viewer_window::toggle_slideshow()
{
    if (slideshow_timer_ == nullptr)
    {
        return;
    }

    if (slideshow_action_->isChecked())
    {
        slideshow_timer_->start();
    }
    else
    {
        slideshow_timer_->stop();
    }
}

void image_viewer_window::toggle_slideshow_loop()
{
    slideshow_loop_enabled_ = slideshow_loop_action_ != nullptr && slideshow_loop_action_->isChecked();
}

void image_viewer_window::configure_slideshow_interval()
{
    bool ok = false;
    const int seconds = QInputDialog::getInt(
        this,
        "播放间隔",
        "每张图片停留秒数：",
        slideshow_interval_ms_ / 1000,
        1,
        60,
        1,
        &ok);

    if (!ok)
    {
        return;
    }

    slideshow_interval_ms_ = seconds * 1000;
    if (slideshow_timer_ != nullptr)
    {
        slideshow_timer_->setInterval(slideshow_interval_ms_);
    }
}

void image_viewer_window::advance_slideshow()
{
    if (current_index_ < 0 || image_list_.empty())
    {
        slideshow_timer_->stop();
        slideshow_action_->setChecked(false);
        return;
    }

    if (current_index_ >= static_cast<ptrdiff_t>(image_list_.size()) - 1)
    {
        if (slideshow_loop_enabled_)
        {
            set_image_path(image_list_.constFirst());
            return;
        }
        slideshow_timer_->stop();
        slideshow_action_->setChecked(false);
        return;
    }

    load_next_image();
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

    set_image_path(image_list_.at(new_idx));
}

void image_viewer_window::update_navigation_buttons()
{
    bool has_prev = current_index_ > 0;
    bool has_next = current_index_ >= 0 && current_index_ < static_cast<ptrdiff_t>(image_list_.size()) - 1;

    prev_action_->setEnabled(has_prev);
    next_action_->setEnabled(has_next);
    position_label_->setText(current_index_ < 0 ? QString() : QString("%1 / %2").arg(current_index_ + 1).arg(image_list_.size()));
}

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
    else if (event->key() == Qt::Key_Space)
    {
        load_next_image();
    }
    else if (event->key() == Qt::Key_Backspace)
    {
        load_prev_image();
    }
    else if (event->key() == Qt::Key_Home)
    {
        if (!image_list_.empty())
        {
            set_image_path(image_list_.constFirst());
        }
    }
    else if (event->key() == Qt::Key_End)
    {
        if (!image_list_.empty())
        {
            set_image_path(image_list_.constLast());
        }
    }
    else if (event->key() == Qt::Key_Escape)
    {
        if (isFullScreen())
        {
            showNormal();
            update_view_mode_actions();
        }
        else
        {
            close();
        }
    }
    else if (event->key() == Qt::Key_Delete)
    {
        if (!current_path_.isEmpty())
        {
            emit request_move_to_trash(current_path_);
        }
    }
    else
    {
        QMainWindow::keyPressEvent(event);
    }
}

void image_viewer_window::closeEvent(QCloseEvent* event)
{
    save_settings();
    clear_movie();

    scene_->clear();
    image_item_ = nullptr;
    current_path_.clear();
    image_list_ = QStringList();
    current_index_ = -1;
    if (slideshow_timer_ != nullptr)
    {
        slideshow_timer_->stop();
    }
    slideshow_action_->setChecked(false);
    rotation_degrees_ = 0;
    flip_horizontal_ = false;
    flip_vertical_ = false;
    image_info_label_->clear();
    zoom_label_->clear();
    has_manual_zoom_ = false;
    update_navigation_buttons();
    update_view_mode_actions();
    QMainWindow::closeEvent(event);
}
