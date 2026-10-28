#include <QTimer>
#include <QDebug>
#include <QAction>
#include <QActionGroup>
#include <QKeySequence>
#include <QFileDialog>
#include <QDirIterator>
#include <QDir>
#include <QImageReader>
#include <QStatusBar>
#include <QCoreApplication>
#include <QResizeEvent>
#include <QFileInfo>
#include <QFile>
#include <QFontMetrics>
#include <QMenu>
#include <QMimeData>
#include <QMessageBox>
#include <QUrl>
#include <QSettings>
#include <QDesktopServices>
#include <QProcess>
#include <QStandardPaths>
#include <QToolBar>
#include <QApplication>
#include <QProgressBar>
#include <QStyle>
#include <QPalette>
#include <QSignalBlocker>
#include <QToolButton>

#include "main_window.h"
#include "image_loader.h"
#include "common_types.h"
#include "waterfall_view.h"
#include "waterfall_scene.h"
#include "waterfall_item.h"
#include "image_viewer_window.h"
#include "file_scanner.h"
#include "window_title_bar.h"

main_window::main_window(QWidget* parent) : QMainWindow(parent)
{
    setup_ui();
    setup_worker();
    setup_scanner();
    setup_connections();
    last_open_dir_ = QDir::homePath();
    load_settings();
}

main_window::~main_window()
{
    if (image_loader_ != nullptr)
    {
        image_loader_->stop();
    }
    if (scene_ != nullptr)
    {
        scene_->disconnect(this);
    }
    if (worker_thread_ != nullptr)
    {
        worker_thread_->quit();
        worker_thread_->wait();
    }

    if (file_scanner_ != nullptr)
    {
        file_scanner_->stop_scan();
    }
    if (scan_thread_ != nullptr)
    {
        scan_thread_->quit();
        scan_thread_->wait();
    }
}

void main_window::setup_ui()
{
    auto rescan_current_folder =
        [this]()
    {
        if (current_root_path_.isEmpty())
        {
            return;
        }
        open_path(current_root_path_, false);
    };

    setWindowTitle("ImageViewer");
    auto* act_open = new QAction(QIcon(":/icons/folder.svg"), "打开", this);
    act_open->setToolTip("打开（Ctrl+O）");
    act_open->setShortcut(QKeySequence::Open);
    addAction(act_open);
    connect(act_open, &QAction::triggered, this, &main_window::on_add_folder);

    auto* act_open_file = new QAction(QIcon(":/icons/image.svg"), "打开图片", this);
    act_open_file->setShortcut(QKeySequence("Ctrl+Shift+O"));
    addAction(act_open_file);
    connect(act_open_file,
            &QAction::triggered,
            this,
            [this]()
            {
                QString path = QFileDialog::getOpenFileName(
                    this,
                    "Select Image",
                    last_open_dir_,
                    "Images (*.jpg *.jpeg *.png *.bmp *.gif *.webp)");
                if (!path.isEmpty())
                {
                    open_path(path, true);
                }
            });

    auto* act_open_recent = new QAction(QIcon(":/icons/history.svg"), "最近", this);
    act_open_recent->setShortcut(QKeySequence("Ctrl+Alt+O"));
    addAction(act_open_recent);
    auto* act_shortcuts = new QAction("快捷键", this);
    act_shortcuts->setShortcut(QKeySequence(Qt::Key_F1));
    addAction(act_shortcuts);
    connect(act_shortcuts,
            &QAction::triggered,
            this,
            [this]()
            {
                QMessageBox::information(
                    this,
                    "快捷键",
                    "主窗口:\n"
                    "Ctrl+O 打开文件夹\n"
                    "Ctrl+Shift+O 打开图片\n"
                    "Ctrl+Alt+O 打开最近项\n"
                    "Enter/Space 打开所选图片\n"
                    "Alt+1..9 打开最近菜单中的对应项目\n"
                    "F1 查看快捷键\n\n"
                    "预览窗口:\n"
                    "Left/Right 上一张/下一张\n"
                    "Space/Backspace 下一张/上一张\n"
                    "鼠标侧键 后退/前进翻图\n"
                    "Ctrl+滚轮 缩放\n"
                    "Ctrl+0 适应窗口\n"
                    "Ctrl+1 1:1\n"
                    "Ctrl+2 适应宽度\n"
                    "F11 全屏\n"
                    "F5 幻灯片\n"
                    "Esc 退出全屏或返回图库");
            });

    auto* toolbar = new QToolBar("浏览", this);
    toolbar->setObjectName("browse_toolbar");
    toolbar->setMovable(false);
    toolbar->setFloatable(false);
    toolbar->setIconSize(QSize(18, 18));
    toolbar->setToolButtonStyle(Qt::ToolButtonIconOnly);
    toolbar->setStyleSheet("QToolBar { background: transparent; border: 0; spacing: 4px; padding: 4px; } QToolButton { padding: 6px; }");
    auto* open_button = new QToolButton(toolbar);
    open_button->setToolButtonStyle(Qt::ToolButtonIconOnly);
    open_button->setDefaultAction(act_open);
    open_button->setPopupMode(QToolButton::MenuButtonPopup);
    auto* open_menu = new QMenu(open_button);
    open_menu->addAction(act_open_file);
    open_button->setMenu(open_menu);
    toolbar->addWidget(open_button);
    toolbar->addAction(act_open_recent);
    auto* recent_button = toolbar->widgetForAction(act_open_recent);
    connect(act_open_recent, &QAction::triggered, this, [this, recent_button]()
    {
        show_recent_menu(recent_button->mapToGlobal(QPoint(0, recent_button->height())));
    });
    auto* spacer = new QWidget(toolbar);
    spacer->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    spacer->setAttribute(Qt::WA_TransparentForMouseEvents);
    toolbar->addWidget(spacer);
    auto* more_button = new QToolButton(toolbar);
    more_button->setText("更多");
    more_button->setIcon(QIcon(":/icons/more.svg"));
    more_button->setToolButtonStyle(Qt::ToolButtonIconOnly);
    more_button->setToolTip("更多");
    more_button->setAccessibleName("更多");
    more_button->setPopupMode(QToolButton::InstantPopup);
    auto* more_menu = new QMenu(more_button);
    more_button->setMenu(more_menu);
    toolbar->addWidget(more_button);
    auto* sort_menu = more_menu->addMenu(QIcon(":/icons/sort.svg"), "排序");

    sort_group_ = new QActionGroup(this);
    sort_group_->setExclusive(true);

    sort_name_action_ = sort_menu->addAction("文件名");
    sort_name_action_->setCheckable(true);
    sort_name_action_->setChecked(true);
    sort_group_->addAction(sort_name_action_);
    connect(sort_name_action_,
            &QAction::triggered,
            this,
            [this, rescan_current_folder]()
            {
                if (current_sort_mode_ == scan_sort_mode::file_name)
                {
                    return;
                }
                current_sort_mode_ = scan_sort_mode::file_name;
                rescan_current_folder();
            });

    sort_time_action_ = sort_menu->addAction("修改时间");
    sort_time_action_->setCheckable(true);
    sort_group_->addAction(sort_time_action_);
    connect(sort_time_action_,
            &QAction::triggered,
            this,
            [this, rescan_current_folder]()
            {
                if (current_sort_mode_ == scan_sort_mode::modified_time)
                {
                    return;
                }
                current_sort_mode_ = scan_sort_mode::modified_time;
                rescan_current_folder();
            });

    sort_size_action_ = sort_menu->addAction("文件大小");
    sort_size_action_->setCheckable(true);
    sort_group_->addAction(sort_size_action_);
    connect(sort_size_action_,
            &QAction::triggered,
            this,
            [this, rescan_current_folder]()
            {
                if (current_sort_mode_ == scan_sort_mode::file_size)
                {
                    return;
                }
                current_sort_mode_ = scan_sort_mode::file_size;
                rescan_current_folder();
            });

    sort_menu->addSeparator();
    sort_desc_action_ = sort_menu->addAction("倒序");
    sort_desc_action_->setCheckable(true);
    connect(sort_desc_action_,
            &QAction::triggered,
            this,
            [this, rescan_current_folder]()
            {
                const bool descending = sort_desc_action_ != nullptr && sort_desc_action_->isChecked();
                if (sort_descending_ == descending)
                {
                    return;
                }
                sort_descending_ = descending;
                rescan_current_folder();
            });

    more_menu->addSeparator();
    dark_theme_action_ = more_menu->addAction(QIcon(":/icons/theme.svg"), "深色主题");
    dark_theme_action_->setObjectName("dark_theme");
    dark_theme_action_->setCheckable(true);
    connect(dark_theme_action_, &QAction::toggled, this, &main_window::set_dark_theme);
    more_menu->addAction(act_shortcuts);

    scene_ = new waterfall_scene(this);
    view_ = new waterfall_view(this);
    view_->setScene(scene_);
    auto* open_selected_action = new QAction("打开所选图片", view_);
    open_selected_action->setShortcuts({QKeySequence(Qt::Key_Return), QKeySequence(Qt::Key_Enter), QKeySequence(Qt::Key_Space)});
    open_selected_action->setShortcutContext(Qt::WidgetWithChildrenShortcut);
    view_->addAction(open_selected_action);
    connect(open_selected_action, &QAction::triggered, this, [this]()
    {
        const auto selected = scene_->selectedItems();
        if (selected.isEmpty())
        {
            return;
        }
        auto* item = dynamic_cast<waterfall_item*>(selected.first());
        if (item != nullptr)
        {
            on_image_double_clicked(item->get_path());
        }
    });

    setCentralWidget(view_);
    setAcceptDrops(true);

    status_label_ = new QLabel("打开或拖入文件夹，开始浏览图片", this);
    statusBar()->addWidget(status_label_);
    scan_progress_ = new QProgressBar(this);
    scan_progress_->setObjectName("scan_progress");
    scan_progress_->setMaximumWidth(160);
    scan_progress_->setTextVisible(false);
    scan_progress_->hide();
    statusBar()->addPermanentWidget(scan_progress_);

    info_label_ = new QLabel(this);
    statusBar()->addPermanentWidget(info_label_);
    setMenuWidget(new window_title_bar(this, toolbar));
}

void main_window::setup_worker()
{
    worker_thread_ = new QThread(this);
    image_loader_ = new image_loader();
    image_loader_->moveToThread(worker_thread_);
    connect(worker_thread_, &QThread::finished, image_loader_, &QObject::deleteLater);
    worker_thread_->start();
}

void main_window::setup_scanner()
{
    scan_thread_ = new QThread(this);
    file_scanner_ = new file_scanner();

    file_scanner_->moveToThread(scan_thread_);

    connect(scan_thread_, &QThread::finished, file_scanner_, &QObject::deleteLater);

    connect(this, &main_window::request_start_scan, file_scanner_, &file_scanner::start_scan);
    connect(file_scanner_, &file_scanner::scan_prepared, this, [this](qsizetype count, int session)
    {
        if (session == current_scan_session_id_)
        {
            scene_->reserve_images(count);
            scan_progress_->setRange(0, std::max<qsizetype>(1, count));
        }
    });
    connect(file_scanner_, &file_scanner::scan_failed, this, [this](const QString& message, int session)
    {
        if (session != current_scan_session_id_)
        {
            return;
        }
        is_scanning_ = false;
        scan_progress_->hide();
        status_label_->setText(message);
        scene_->set_empty_text(message);
    });
    connect(file_scanner_, &file_scanner::images_scanned_batch, this, &main_window::on_scan_batch_received);
    connect(file_scanner_, &file_scanner::scan_finished, this, &main_window::on_scan_all_finished);

    scan_thread_->start();
}

void main_window::setup_connections()
{
    connect(scene_, &waterfall_scene::request_load_batch, image_loader_, &image_loader::request_thumbnails, Qt::DirectConnection);
    connect(scene_, &waterfall_scene::request_cancel_batch, image_loader_, &image_loader::cancel_thumbnails, Qt::DirectConnection);
    connect(scene_, &waterfall_scene::request_prioritize_batch, image_loader_, &image_loader::prioritize_thumbnails, Qt::DirectConnection);
    connect(scene_, &waterfall_scene::request_cancel_all, image_loader_, &image_loader::clear_all, Qt::DirectConnection);
    connect(image_loader_, &image_loader::thumbnail_loaded, scene_, &waterfall_scene::on_image_loaded, Qt::QueuedConnection);

    connect(view_, &waterfall_view::view_resized, this, [this](int width) { scene_->layout_models(width); });

    connect(scene_, &waterfall_scene::image_double_clicked, this, &main_window::on_image_double_clicked);
    connect(scene_, &waterfall_scene::request_open_folder, this, &main_window::on_add_folder);
    connect(scene_, &waterfall_scene::request_open_recent, this, &main_window::on_open_recent_path);
    connect(scene_, &waterfall_scene::request_reveal_path, this, &main_window::on_reveal_path);
    connect(scene_, &waterfall_scene::request_move_to_trash, this, &main_window::on_move_path_to_trash);
    connect(scene_, &waterfall_scene::request_clear_thumbnail_cache, image_loader_, &image_loader::clear_cache);
    connect(scene_,
            &waterfall_scene::request_clear_thumbnail_cache,
            this,
            [this]()
            {
                statusBar()->showMessage("已清理缩略图缓存", 3000);
            });
    connect(scene_, &QGraphicsScene::selectionChanged, this, &main_window::on_selection_changed);
}

void main_window::load_settings()
{
    QSettings settings("gyl30", "ImageViewer");

    restoreGeometry(settings.value("main_window/geometry").toByteArray());
    const bool dark = settings.value("main_window/dark_theme", QApplication::palette().base().color().lightness() < 128).toBool();
    const QSignalBlocker theme_signals(dark_theme_action_);
    dark_theme_action_->setChecked(dark);
    set_dark_theme(dark);

    recent_folder_paths_ = settings.value("main_window/recent_folder_paths").toStringList();
    recent_image_paths_ = settings.value("main_window/recent_image_paths").toStringList();
    prune_recent_paths();

    const int sort_mode = settings.value("main_window/sort_mode", static_cast<int>(scan_sort_mode::file_name)).toInt();
    if (sort_mode == static_cast<int>(scan_sort_mode::modified_time))
    {
        current_sort_mode_ = scan_sort_mode::modified_time;
    }
    else if (sort_mode == static_cast<int>(scan_sort_mode::file_size))
    {
        current_sort_mode_ = scan_sort_mode::file_size;
    }
    else
    {
        current_sort_mode_ = scan_sort_mode::file_name;
    }

    sort_descending_ = settings.value("main_window/sort_descending", false).toBool();
    if (sort_name_action_ != nullptr)
    {
        sort_name_action_->setChecked(current_sort_mode_ == scan_sort_mode::file_name);
    }
    if (sort_time_action_ != nullptr)
    {
        sort_time_action_->setChecked(current_sort_mode_ == scan_sort_mode::modified_time);
    }
    if (sort_size_action_ != nullptr)
    {
        sort_size_action_->setChecked(current_sort_mode_ == scan_sort_mode::file_size);
    }
    if (sort_desc_action_ != nullptr)
    {
        sort_desc_action_->setChecked(sort_descending_);
    }

    QString saved_dir = settings.value("main_window/last_open_dir", QDir::homePath()).toString();
    if (QFileInfo::exists(saved_dir))
    {
        last_open_dir_ = saved_dir;
    }

    if (!isVisible() && !settings.contains("main_window/geometry"))
    {
        resize(1024, 768);
    }
}

void main_window::save_settings() const
{
    QSettings settings("gyl30", "ImageViewer");
    settings.setValue("main_window/geometry", saveGeometry());
    settings.setValue("main_window/dark_theme", dark_theme_action_->isChecked());
    settings.setValue("main_window/recent_folder_paths", recent_folder_paths_);
    settings.setValue("main_window/recent_image_paths", recent_image_paths_);
    settings.setValue("main_window/last_open_dir", last_open_dir_);
    settings.setValue("main_window/sort_mode", static_cast<int>(current_sort_mode_));
    settings.setValue("main_window/sort_descending", sort_descending_);
}

void main_window::prune_recent_paths()
{
    recent_folder_paths_.erase(std::remove_if(recent_folder_paths_.begin(),
                                              recent_folder_paths_.end(),
                                              [](const QString& path) { return !QFileInfo(path).isDir(); }),
                               recent_folder_paths_.end());

    recent_image_paths_.erase(std::remove_if(recent_image_paths_.begin(),
                                             recent_image_paths_.end(),
                                             [](const QString& path) { return !QFileInfo(path).isFile(); }),
                              recent_image_paths_.end());

    if (scene_ != nullptr)
    {
        scene_->set_recent_paths(recent_folder_paths_, recent_image_paths_);
    }
}

void main_window::on_add_folder()
{
    QFileDialog dialog(this, "Select Folder", last_open_dir_);
    dialog.setFileMode(QFileDialog::Directory);
    dialog.setOption(QFileDialog::DontUseNativeDialog, true);
    if (dialog.exec() == 0)
    {
        return;
    }
    QString dir_path = dialog.selectedFiles().first();
    if (dir_path.isEmpty())
    {
        return;
    }

    open_path(dir_path, true);
}

void main_window::open_path(const QString& path, bool add_to_recent)
{
    QFileInfo file_info(path);
    if (!file_info.exists())
    {
        statusBar()->showMessage("路径不存在，请检查后重试", 4000);
        return;
    }

    if (file_info.isFile())
    {
        last_open_dir_ = file_info.absolutePath();
        QStringList image_list = scene_->get_all_paths();
        if (!image_list.contains(path))
        {
            image_list = {path};
        }

        if (add_to_recent)
        {
            add_recent_path(path);
        }
        show_image_viewer(path, image_list);
        return;
    }

    last_open_dir_ = path;
    current_root_path_ = path;

    current_scan_session_id_ = file_scanner_->stop_scan();

    scene_->clear_items();
    scene_->set_empty_text("正在查找图片…");
    total_count_ = 0;
    scan_duration_ = 0;
    info_label_->clear();

    is_scanning_ = true;
    scan_progress_->setRange(0, 0);
    scan_progress_->show();
    status_label_->setText("正在查找图片…");

    if (add_to_recent)
    {
        add_recent_path(path);
    }

    emit request_start_scan(path, current_scan_session_id_, static_cast<int>(current_sort_mode_), sort_descending_);
    view_->setFocus(Qt::OtherFocusReason);
}

void main_window::add_recent_path(const QString& path)
{
    static constexpr qsizetype kMaxRecentPaths = 10;

    QStringList* recent_paths = QFileInfo(path).isDir() ? &recent_folder_paths_ : &recent_image_paths_;

    recent_paths->removeAll(path);
    recent_paths->prepend(path);

    while (recent_paths->size() > kMaxRecentPaths)
    {
        recent_paths->removeLast();
    }

    scene_->set_recent_paths(recent_folder_paths_, recent_image_paths_);
}

void main_window::show_recent_menu(const QPoint& global_pos)
{
    prune_recent_paths();

    QMenu menu(this);

    const QStringList paths = recent_folder_paths_ + recent_image_paths_;
    for (qsizetype index = 0; index < paths.size(); ++index)
    {
        const QString& path = paths[index];
        const QFileInfo info(path);
        const QString name = info.fileName().isEmpty() ? info.absoluteFilePath() : info.fileName();
        QString label = menu.fontMetrics().elidedText(name, Qt::ElideMiddle, 320);
        label.replace('&', "&&");
        if (index < 9)
        {
            label = QString("&%1 %2").arg(index + 1).arg(label);
        }
        auto* action = menu.addAction(QIcon(info.isDir() ? ":/icons/folder.svg" : ":/icons/image.svg"), label);
        action->setData(path);
        if (index < 9)
        {
            action->setShortcut(QKeySequence(QString("Alt+%1").arg(index + 1)));
        }
        connect(action, &QAction::triggered, this, [this, path]() { open_path(path, true); });
    }

    if (menu.isEmpty())
    {
        QAction* empty_action = menu.addAction("暂无最近记录");
        empty_action->setEnabled(false);
    }

    menu.exec(global_pos);
}

void main_window::show_image_viewer(const QString& path, const QStringList& image_list)
{
    if (viewer_window_ == nullptr)
    {
        viewer_window_ = new image_viewer_window(this);
        connect(viewer_window_,
                &image_viewer_window::current_image_changed,
                this,
                [this](const QString& path)
                {
                    if (scene_ != nullptr)
                    {
                        scene_->focus_path(path);
                    }
                });
        connect(viewer_window_, &image_viewer_window::request_move_to_trash, this, &main_window::on_move_path_to_trash);
    }

    viewer_window_->set_image_list(image_list);
    viewer_window_->set_image_path(path);

    if (viewer_window_->isMinimized())
    {
        viewer_window_->showNormal();
    }
    viewer_window_->show();
    viewer_window_->raise();
    viewer_window_->activateWindow();
}

void main_window::on_scan_batch_received(const QList<image_meta>& batch, int session_id)
{
    if (session_id != current_scan_session_id_)
    {
        return;
    }

    scene_->add_images(batch);
    total_count_ += static_cast<int>(batch.size());
    scan_progress_->setValue(total_count_);
    scene_->layout_models(view_->viewport()->width());
    update_status_bar();
}

void main_window::on_scan_all_finished(int total, qint64 duration, int session_id)
{
    if (session_id != current_scan_session_id_)
    {
        return;
    }

    scan_duration_ = duration;
    is_scanning_ = false;
    scan_progress_->hide();
    total_count_ = total;
    if (total == 0)
    {
        scene_->set_empty_text("此文件夹没有可读取的图片");
    }
    scene_->layout_models(view_->viewport()->width());
    update_status_bar();
}

void main_window::update_status_bar()
{
    if (is_scanning_)
    {
        status_label_->setText(QString("正在读取图片信息… %1 张").arg(total_count_));
        return;
    }
    status_label_->setText(total_count_ == 0 ? "此文件夹没有可读取的图片" :
        QString("%1 张图片 · 扫描 %2 秒").arg(total_count_).arg(scan_duration_ / 1000.0, 0, 'f', 2));
}

void main_window::on_selection_changed()
{
    QList<QGraphicsItem*> items = scene_->selectedItems();

    if (items.isEmpty())
    {
        info_label_->clear();
        return;
    }

    auto* item = dynamic_cast<waterfall_item*>(items.first());
    if (item == nullptr)
    {
        return;
    }

    QFileInfo file_info(item->get_path());
    QString file_name = file_info.fileName();
    qint64 file_bytes = file_info.size();
    QSize img_size = item->get_original_size();

    QString size_str;
    if (file_bytes < 1024)
    {
        size_str = QString("%1 B").arg(file_bytes);
    }
    else if (file_bytes < static_cast<qint64>(1024 * 1024))
    {
        size_str = QString("%1 KB").arg(static_cast<double>(file_bytes) / 1024.0, 0, 'f', 1);
    }
    else
    {
        size_str = QString("%1 MB").arg(static_cast<double>(file_bytes) / (1024.0 * 1024.0), 0, 'f', 2);
    }

    info_label_->setText(QString("%1 | %2x%3 | %4").arg(file_name).arg(img_size.width()).arg(img_size.height()).arg(size_str));
}

void main_window::on_image_double_clicked(const QString& path)
{
    show_image_viewer(path, scene_->get_all_paths());
}

void main_window::on_open_recent_path(const QString& path)
{
    open_path(path, true);
}

void main_window::on_reveal_path(const QString& path)
{
    QFileInfo file_info(path);
    if (!file_info.exists())
    {
        return;
    }

    bool handled = false;

#if defined(Q_OS_WIN)
    handled = QProcess::startDetached("explorer.exe", {"/select,", QDir::toNativeSeparators(file_info.absoluteFilePath())});
#elif defined(Q_OS_MACOS)
    handled = QProcess::startDetached("open", {"-R", file_info.absoluteFilePath()});
#else
    if (file_info.isFile())
    {
        const QString dbus_send = QStandardPaths::findExecutable("dbus-send");
        if (!dbus_send.isEmpty())
        {
            const QString uri = QUrl::fromLocalFile(file_info.absoluteFilePath()).toString();
            handled = QProcess::startDetached(
                dbus_send,
                {"--session",
                 "--dest=org.freedesktop.FileManager1",
                 "--type=method_call",
                 "/org/freedesktop/FileManager1",
                 "org.freedesktop.FileManager1.ShowItems",
                 QString("array:string:%1").arg(uri),
                 "string:"});
        }
    }
#endif

    if (!handled)
    {
        const QString reveal_target = file_info.isDir() ? file_info.absoluteFilePath() : file_info.absolutePath();
        QDesktopServices::openUrl(QUrl::fromLocalFile(reveal_target));
    }
}

void main_window::on_move_path_to_trash(const QString& path)
{
    QFileInfo file_info(path);
    const QMessageBox::StandardButton reply = QMessageBox::question(
        this,
        "移到回收站",
        QString("确认将 \"%1\" 移到回收站吗？").arg(file_info.fileName()),
        QMessageBox::Yes | QMessageBox::No,
        QMessageBox::No);
    if (reply != QMessageBox::Yes)
    {
        return;
    }

#ifdef Q_OS_LINUX
    // Qt 的回收站实现要求 XDG 数据根已存在。
    const QString data_home = QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation);
    QDir().mkpath(QFileInfo(data_home).absolutePath());
    QDir().mkdir(data_home, QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner);
#endif
    if (!QFile::moveToTrash(path))
    {
        QMessageBox::warning(this, "移到回收站失败", QString("无法将 \"%1\" 移到回收站。").arg(file_info.fileName()));
        return;
    }

    recent_image_paths_.removeAll(path);
    scene_->set_recent_paths(recent_folder_paths_, recent_image_paths_);

    if (viewer_window_ != nullptr)
    {
        viewer_window_->remove_image_path(path);
    }

    if (!current_root_path_.isEmpty() && QFileInfo::exists(current_root_path_))
    {
        open_path(current_root_path_, false);
    }
}

void main_window::closeEvent(QCloseEvent* event)
{
    if (viewer_window_ != nullptr)
    {
        viewer_window_->close();
    }
    save_settings();
    QMainWindow::closeEvent(event);
}

void main_window::set_dark_theme(bool enabled)
{
    QPalette palette = QApplication::style()->standardPalette();
    if (enabled)
    {
        const QColor text("#eef1f6");
        const QColor disabled("#778294");
        const QColor alternate("#292e37");
        palette.setColor(QPalette::Window, QColor("#17191e"));
        palette.setColor(QPalette::Base, QColor("#20232a"));
        palette.setColor(QPalette::AlternateBase, alternate);
        palette.setColor(QPalette::Button, QColor("#262b33"));
        palette.setColor(QPalette::Text, text);
        palette.setColor(QPalette::WindowText, text);
        palette.setColor(QPalette::ButtonText, text);
        palette.setColor(QPalette::PlaceholderText, QColor("#a2adbf"));
        palette.setColor(QPalette::Mid, QColor("#738097"));
        palette.setColor(QPalette::Highlight, QColor("#4b94ee"));
        palette.setColor(QPalette::HighlightedText, Qt::white);
        palette.setColor(QPalette::Disabled, QPalette::Text, disabled);
        palette.setColor(QPalette::Disabled, QPalette::ButtonText, disabled);
        palette.setColor(QPalette::ToolTipBase, alternate);
        palette.setColor(QPalette::ToolTipText, text);
    }
    QApplication::setPalette(palette);
    for (auto* view : findChildren<QGraphicsView*>())
    {
        view->setBackgroundBrush(palette.base());
    }
    scene_->update();
}

void main_window::dragEnterEvent(QDragEnterEvent* event)
{
    if (event->mimeData()->hasUrls())
    {
        const QList<QUrl> urls = event->mimeData()->urls();
        if (!urls.isEmpty() && urls.first().isLocalFile())
        {
            event->acceptProposedAction();
            return;
        }
    }

    QMainWindow::dragEnterEvent(event);
}

void main_window::dropEvent(QDropEvent* event)
{
    const QList<QUrl> urls = event->mimeData()->urls();
    for (const QUrl& url : urls)
    {
        if (!url.isLocalFile())
        {
            continue;
        }

        QString local_path = url.toLocalFile();
        if (QFileInfo::exists(local_path))
        {
            open_path(local_path, true);
            event->acceptProposedAction();
            return;
        }
    }

    QMainWindow::dropEvent(event);
}
