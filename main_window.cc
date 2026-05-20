#include <QTimer>
#include <QDebug>
#include <QAction>
#include <QActionGroup>
#include <QKeySequence>
#include <QFileDialog>
#include <QDirIterator>
#include <QImageReader>
#include <QStatusBar>
#include <QCoreApplication>
#include <QResizeEvent>
#include <QFileInfo>
#include <QFile>
#include <QMenu>
#include <QMimeData>
#include <QMessageBox>
#include <QUrl>
#include <QSettings>
#include <QDesktopServices>
#include <QProcess>
#include <QStandardPaths>
#include <QToolBar>
#include <vector>

#include "main_window.h"
#include "image_loader.h"
#include "common_types.h"
#include "waterfall_view.h"
#include "waterfall_scene.h"
#include "waterfall_item.h"
#include "image_viewer_window.h"
#include "file_scanner.h"

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

    auto* act_open = new QAction("Open", this);
    act_open->setShortcut(QKeySequence::Open);
    addAction(act_open);
    connect(act_open, &QAction::triggered, this, &main_window::on_add_folder);

    auto* act_open_file = new QAction("Open Image", this);
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

    auto* act_open_recent = new QAction("Open Recent", this);
    act_open_recent->setShortcut(QKeySequence("Ctrl+Alt+O"));
    addAction(act_open_recent);
    connect(act_open_recent,
            &QAction::triggered,
            this,
            [this]()
            {
                QRect rect = geometry();
                show_recent_menu(mapToGlobal(QPoint(rect.width() / 2, rect.height() / 2)));
            });

    auto* act_shortcuts = new QAction("Shortcuts", this);
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
                    "Alt+1..9 打开最近菜单中的对应项目\n"
                    "F1 查看快捷键\n\n"
                    "预览窗口:\n"
                    "Left/Right 上一张/下一张\n"
                    "Ctrl+滚轮 缩放\n"
                    "Ctrl+0 适应窗口\n"
                    "Ctrl+1 1:1\n"
                    "Ctrl+2 适应宽度\n"
                    "F11 全屏\n"
                    "F5 幻灯片");
            });

    auto* sort_toolbar = addToolBar("Sort");
    sort_toolbar->setMovable(false);

    sort_group_ = new QActionGroup(this);
    sort_group_->setExclusive(true);

    sort_name_action_ = sort_toolbar->addAction("文件名");
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

    sort_time_action_ = sort_toolbar->addAction("修改时间");
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

    sort_size_action_ = sort_toolbar->addAction("文件大小");
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

    sort_toolbar->addSeparator();

    sort_desc_action_ = sort_toolbar->addAction("倒序");
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

    scene_ = new waterfall_scene(this);
    view_ = new waterfall_view(this);
    view_->setScene(scene_);

    setCentralWidget(view_);
    setAcceptDrops(true);

    status_label_ = new QLabel("Press Ctrl+O to load folder", this);
    statusBar()->addWidget(status_label_);

    info_label_ = new QLabel(this);
    statusBar()->addPermanentWidget(info_label_);
}

void main_window::setup_worker()
{
    worker_thread_ = new QThread(this);
    image_loader_ = new image_loader();
    image_loader_->moveToThread(worker_thread_);
    connect(worker_thread_, &QThread::finished, image_loader_, &QObject::deleteLater);
    connect(worker_thread_, &QThread::started, image_loader_, &image_loader::start_loop);
    worker_thread_->start();
}

void main_window::setup_scanner()
{
    scan_thread_ = new QThread(this);
    file_scanner_ = new file_scanner();

    file_scanner_->moveToThread(scan_thread_);

    connect(scan_thread_, &QThread::finished, file_scanner_, &QObject::deleteLater);

    connect(this, &main_window::request_start_scan, file_scanner_, &file_scanner::start_scan);
    connect(file_scanner_, &file_scanner::images_scanned_batch, this, &main_window::on_scan_batch_received);
    connect(file_scanner_, &file_scanner::scan_finished, this, &main_window::on_scan_all_finished);

    scan_thread_->start();
}

void main_window::setup_connections()
{
    connect(scene_, &waterfall_scene::request_load_batch, image_loader_, &image_loader::request_thumbnails, Qt::DirectConnection);
    connect(scene_, &waterfall_scene::request_cancel_batch, image_loader_, &image_loader::cancel_thumbnails, Qt::DirectConnection);
    connect(scene_, &waterfall_scene::request_cancel_all, image_loader_, &image_loader::clear_all, Qt::DirectConnection);
    connect(image_loader_, &image_loader::thumbnail_loaded, scene_, &waterfall_scene::on_image_loaded);
    connect(image_loader_, &image_loader::tasks_dropped, scene_, &waterfall_scene::on_tasks_dropped);

    connect(view_, &waterfall_view::view_resized, this, [this](int width) { scene_->layout_models(width); });

    connect(scene_, &waterfall_scene::image_double_clicked, this, &main_window::on_image_double_clicked);
    connect(scene_, &waterfall_scene::request_open_folder, this, &main_window::on_add_folder);
    connect(scene_, &waterfall_scene::request_open_recent, this, &main_window::on_open_recent_path);
    connect(scene_, &waterfall_scene::request_reveal_path, this, &main_window::on_reveal_path);
    connect(scene_, &waterfall_scene::request_move_to_trash, this, &main_window::on_move_path_to_trash);
    connect(scene_, &QGraphicsScene::selectionChanged, this, &main_window::on_selection_changed);
    connect(image_loader_, &image_loader::thumbnail_loaded, this, &main_window::on_image_loaded_stat);
}

void main_window::load_settings()
{
    QSettings settings("gyl30", "ImageViewer");

    restoreGeometry(settings.value("main_window/geometry").toByteArray());

    recent_folder_paths_ = settings.value("main_window/recent_folder_paths").toStringList();
    recent_folder_paths_.erase(std::remove_if(recent_folder_paths_.begin(),
                                              recent_folder_paths_.end(),
                                              [](const QString& path) { return !QFileInfo(path).isDir(); }),
                               recent_folder_paths_.end());

    recent_image_paths_ = settings.value("main_window/recent_image_paths").toStringList();
    recent_image_paths_.erase(std::remove_if(recent_image_paths_.begin(),
                                             recent_image_paths_.end(),
                                             [](const QString& path) { return !QFileInfo(path).isFile(); }),
                              recent_image_paths_.end());

    scene_->set_recent_paths(recent_folder_paths_, recent_image_paths_);

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
    settings.setValue("main_window/recent_folder_paths", recent_folder_paths_);
    settings.setValue("main_window/recent_image_paths", recent_image_paths_);
    settings.setValue("main_window/last_open_dir", last_open_dir_);
    settings.setValue("main_window/sort_mode", static_cast<int>(current_sort_mode_));
    settings.setValue("main_window/sort_descending", sort_descending_);
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
        return;
    }

    if (file_info.isFile())
    {
        last_open_dir_ = file_info.absolutePath();
        std::vector<QString> image_list = scene_->get_all_paths();
        if (std::find(image_list.begin(), image_list.end(), path) == image_list.end())
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

    if (file_scanner_ != nullptr)
    {
        file_scanner_->stop_scan();
    }

    scene_->clear_items();
    total_count_ = 0;
    loaded_count_ = 0;
    loaded_paths_.clear();
    scan_duration_ = 0;
    info_label_->clear();

    current_scan_session_id_++;

    status_label_->setText(QString("Scanning: %1").arg(path));

    if (add_to_recent)
    {
        add_recent_path(path);
    }

    emit request_start_scan(path, current_scan_session_id_, static_cast<int>(current_sort_mode_), sort_descending_);
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
    QMenu menu(this);

    auto add_recent_actions =
        [this, &menu](const QString& title, const QStringList& paths)
        {
            if (paths.isEmpty())
            {
                return;
            }

            QMenu* sub_menu = menu.addMenu(title);
            for (qsizetype i = 0; i < paths.size(); ++i)
            {
                const QString& path = paths[i];
                QString label = path;
                if (i < 9)
                {
                    label = QString("&%1 %2").arg(i + 1).arg(path);
                }

                QAction* action = sub_menu->addAction(label);
                if (i < 9)
                {
                    action->setShortcut(QKeySequence(QString("Alt+%1").arg(i + 1)));
                }
                connect(action, &QAction::triggered, this, [this, path]() { open_path(path, true); });
            }
        };

    add_recent_actions("最近文件夹", recent_folder_paths_);
    add_recent_actions("最近图片", recent_image_paths_);

    if (menu.isEmpty())
    {
        QAction* empty_action = menu.addAction("暂无最近记录");
        empty_action->setEnabled(false);
    }

    menu.exec(global_pos);
}

void main_window::show_image_viewer(const QString& path, const std::vector<QString>& image_list)
{
    if (viewer_window_ == nullptr)
    {
        viewer_window_ = new image_viewer_window(this);
        viewer_window_->setWindowFlags(Qt::Window);
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
    scene_->layout_models(view_->viewport()->width());
    QMetaObject::invokeMethod(view_, [this]() { view_->check_visible_area(); }, Qt::QueuedConnection);
    update_status_bar();
}

void main_window::on_scan_all_finished(int total, qint64 duration, int session_id)
{
    if (session_id != current_scan_session_id_)
    {
        return;
    }

    scan_duration_ = duration;
    total_count_ = total;
    scene_->layout_models(view_->viewport()->width());
    QMetaObject::invokeMethod(view_, [this]() { view_->check_visible_area(); }, Qt::QueuedConnection);
    update_status_bar();
}

void main_window::on_image_loaded_stat(quint64 /*id*/, const QString& path, const QImage& /*image*/, int session_id)
{
    if (session_id != current_scan_session_id_ || loaded_paths_.contains(path))
    {
        return;
    }

    loaded_paths_.insert(path);
    loaded_count_ = loaded_paths_.size();
    update_status_bar();
}

void main_window::update_status_bar()
{
    QString status = QString("Scan+Layout: %1 ms | Loaded: %2 / %3").arg(scan_duration_).arg(loaded_count_).arg(total_count_);

    if (loaded_count_ == total_count_ && total_count_ > 0)
    {
        status += " [All Done]";
    }
    else if (total_count_ == 0 && scan_duration_ > 0)
    {
        status = "No images found in selected folder.";
    }

    status_label_->setText(status);
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
    save_settings();
    QMainWindow::closeEvent(event);
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
