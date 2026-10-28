cmake_minimum_required(VERSION 3.16)
project(ImageViewerUiReview LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
find_package(Qt6 6.4.2 REQUIRED COMPONENTS Widgets Concurrent Svg Test)
get_filename_component(IV_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../../.." ABSOLUTE)

# 复用现有探针，在构建目录生成仅供候选比较的入口，不改正式构建。
file(READ "${IV_ROOT}/tools/performance_probe.cc" probe)
string(REPLACE "#include \"main_window.h\"" "#include \"main_window.h\"\n#include \"candidate_widgets.h\"\n#include <QToolButton>" probe "${probe}")
string(REPLACE "    window.show();\n    QTest::qWait(200);"
    "    window.show();\n    review_library(window, qEnvironmentVariable(\"IV_UI_ROOT\"));\n    QTest::qWait(200);" probe "${probe}")
string(REPLACE "            window_.dark_theme_action_->setChecked(true);\n            QTest::qWait(300);"
    "            window_.dark_theme_action_->setChecked(true);\n            review_library(window_, directory);\n            QTest::qWait(300);" probe "${probe}")
string(REPLACE "            window_.show_image_viewer(window_.scene_->image_paths_.first(), window_.scene_->get_all_paths());"
    "            window_.show_image_viewer(window_.scene_->image_paths_.first(), window_.scene_->get_all_paths());\n            review_preview(*window_.viewer_window_, *window_.scene_);" probe "${probe}")
string(REPLACE "            window_.open_path(empty.path(), false);"
    "            window_.open_path(empty.path(), false);\n            review_library(window_, empty.path());" probe "${probe}")
string(REPLACE "            window_.screen()->grabWindow(window_.winId()).save(output + \"/light.png\");" [=[
            window_.screen()->grabWindow(window_.winId()).save(output + "/light.png");
            if (!measure_review(window_, output, "light")) { return 6; }
]=] probe "${probe}")
string(REPLACE "            window_.screen()->grabWindow(window_.winId()).save(output + \"/dark.png\");" [=[
            window_.screen()->grabWindow(window_.winId()).save(output + "/dark.png");
            if (!measure_review(window_, output, "dark")) { return 6; }
            const QSize wide = window_.size();
            window_.resize(640, 480);
            QTest::qWait(180);
            if (!wait_idle()) { return 4; }
            window_.screen()->grabWindow(window_.winId()).save(output + "/narrow.png");
            if (!measure_review(window_, output, "narrow")) { return 6; }
            window_.resize(wide);
            QTest::qWait(180);
            if (!wait_idle()) { return 4; }
]=] probe "${probe}")
string(REPLACE "            window_.viewer_window_->screen()->grabWindow(window_.viewer_window_->winId()).save(output + \"/preview.png\");" [=[
            window_.viewer_window_->screen()->grabWindow(window_.viewer_window_->winId()).save(output + "/preview.png");
            if (!measure_review(*window_.viewer_window_, output, "preview")) { return 6; }
            window_.viewer_window_->resize(640, 480);
            QTest::qWait(180);
            window_.viewer_window_->screen()->grabWindow(window_.viewer_window_->winId()).save(output + "/preview-narrow.png");
            if (!measure_review(*window_.viewer_window_, output, "preview_narrow")) { return 6; }
]=] probe "${probe}")
string(REPLACE "            window_.screen()->grabWindow(window_.winId()).save(output + \"/empty.png\");" [=[
            window_.screen()->grabWindow(window_.winId()).save(output + "/empty.png");
            if (!measure_review(window_, output, "empty")) { return 6; }
]=] probe "${probe}")
string(REPLACE "    void sample(const QString& phase, QFile* stream = nullptr)" [=[
    bool measure_review(QWidget& widget, const QString& output, const QString& phase)
    {
        if (phase.contains("narrow") && widget.width() != 640) { return false; }
        auto* header = widget.findChild<QWidget*>("window_title_bar");
        for (auto* button : header->findChildren<QToolButton*>())
        {
            if (button->objectName() == "qt_toolbar_ext_button") { continue; }
            const QRect rect(button->mapTo(&widget, QPoint()), button->size());
            if (!button->isVisibleTo(&widget) || !widget.rect().contains(rect))
            {
                qWarning() << "候选按钮被裁切" << phase << button->text() << rect;
                return false;
            }
        }
        auto* strip = widget.findChild<QWidget*>("review_filmstrip");
        if (strip != nullptr)
        {
            const auto buttons = strip->findChildren<QToolButton*>();
            if (buttons.size() != 7) { return false; }
            for (auto* button : buttons)
            {
                if (phase == "empty" && !button->icon().isNull()) { return false; }
                if (phase.startsWith("preview") && !widget.rect().contains(QRect(button->mapTo(&widget, QPoint()), button->size()))) { return false; }
            }
        }
        const double cpu = process_cpu_ms();
        QTest::qWait(500);
        const double idle = process_cpu_ms() - cpu;
        sample(phase);
        QImage surface(widget.size() * widget.devicePixelRatioF(), QImage::Format_ARGB32_Premultiplied);
        surface.setDevicePixelRatio(widget.devicePixelRatioF());
        std::vector<double> frames;
        for (int frame = 0; frame < 60; ++frame)
        {
            QElapsedTimer elapsed;
            elapsed.start();
            QPainter painter(&surface);
            widget.render(&painter);
            painter.end();
            frames.push_back(elapsed.nsecsElapsed() / 1000000.0);
        }
        std::sort(frames.begin(), frames.end());
        QJsonObject row = samples_.last().toObject();
        row["whole_window_render_frames"] = 60;
        row["whole_window_render_median_ms"] = frames[30];
        row["whole_window_render_p95_ms"] = frames[57];
        row["loaded_idle_cpu_ms_500ms"] = idle;
        row["render_surface_bytes"] = surface.sizeInBytes();
        row["peak_rss_after_render_kib"] = proc_value("/proc/self/status", "VmHWM");
        row["widget_count"] = widget.findChildren<QWidget*>().size();
        row["window_size"] = QJsonArray{widget.width(), widget.height()};
        row["visible_controls_checked"] = true;
        row["filmstrip_cells"] = strip == nullptr ? 0 : strip->findChildren<QToolButton*>().size();
        if (strip != nullptr && phase == "empty") { row["filmstrip_icons_released"] = true; }
        samples_[samples_.size() - 1] = row;
        QFile file(output + "/review-metrics.json");
        if (!file.open(QIODevice::WriteOnly)) { return false; }
        const QByteArray data = QJsonDocument(samples_).toJson();
        return file.write(data) == data.size();
    }

    void sample(const QString& phase, QFile* stream = nullptr)
]=] probe "${probe}")
file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/review_probe.cc" "${probe}")

add_executable(imageviewer_ui_review
    "${CMAKE_CURRENT_BINARY_DIR}/review_probe.cc" candidate_widgets.cc
    "${IV_ROOT}/image_loader.cc" "${IV_ROOT}/waterfall_item.cc"
    "${IV_ROOT}/waterfall_scene.cc" "${IV_ROOT}/waterfall_view.cc"
    "${IV_ROOT}/file_scanner.cc" "${IV_ROOT}/main_window.cc"
    "${IV_ROOT}/image_viewer_window.cc" "${IV_ROOT}/window_title_bar.cc"
    "${IV_ROOT}/resources.qrc")
target_include_directories(imageviewer_ui_review PRIVATE "${IV_ROOT}" "${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(imageviewer_ui_review PRIVATE Qt6::Widgets Qt6::Concurrent Qt6::Svg Qt6::Test)
