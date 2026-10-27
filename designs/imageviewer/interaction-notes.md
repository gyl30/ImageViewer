# ImageViewer 操作交互调整

本页为第一版历史记录；当前实现与用户后续要求见[第二版](interaction-v2-notes.md)。

基线：36f9c16，main 与 origin/main 一致，工作区干净。延续现有 Qt Widgets、系统字体、蓝色选中态与深浅主题。设计文件保存在当前目录，不引入设计系统或产品依赖。

## 现有阻力

- main_window.cc::setup_ui 将打开、最近、路径、三种排序、顺序及主题都放在一行；窄窗口中操作被工具栏溢出菜单隐藏。路径没有 Ctrl+L 入口。
- 图库支持单击选择、双击打开，但没有 Return/Enter/Space 打开所选图片的入口，键盘与鼠标流程不连贯。
- image_viewer_window.cc::display_image 和 start_movie 每次调用 resize_window_to_image，横图、竖图和小图之间切换时窗口及操作目标移动。已有适应模式足以调整画面，无需同时改变窗口。
- 预览工具栏将翻图、三种适应、缩放、旋转、镜像、播放及设置全部平铺；低频设置挤占画面。鼠标侧键不能翻图。

## 本次方案

1. 图库分为操作行和路径行：打开文件夹为主要操作，打开图片放入其下拉菜单；最近保留直接入口；排序使用单个菜单，主题与帮助收进更多。
2. 路径独占可伸缩的一行。Ctrl+L 聚焦并全选，输入后 Enter 打开；图库获得焦点时 Return/Enter/Space 打开所选图，路径输入不触发预览。
3. 预览保持用户设置的窗口尺寸与位置。图片使用既有适应窗口、原始大小、适应宽度；切图、动画首帧均不调整外部窗口。
4. 预览常用操作保留在一行：图库、上一张/下一张、页码、适应方式、缩小/放大、旋转及播放。播放使用与现有图标一致的 SVG 播放/暂停状态；反向旋转、镜像、播放间隔、循环、全屏及帮助进入右侧更多菜单，既有快捷键保留。
5. 鼠标侧键后退/前进与上一张/下一张一致，不循环越界。普通滚轮仍负责平移，Ctrl+滚轮仍缩放；不改变肌肉记忆。
6. 增大工具栏按钮的留白与命中区域，沿用系统控件、焦点和菜单；不加入阴影、模糊、动画、常驻侧栏或逐卡片子控件。

## 验证与范围

先新增回归并观察旧实现失败，再实现窗口稳定、路径快捷键、选图键盘操作和鼠标侧键。随后运行全量 Qt Test，并检查真实 xcb 下的窄窗口、深浅主题及三档静态缩放。

可操作 HTML 用于评审流程与布局，不等同于 Qt 性能或硬件验收。读取现有截图作为视觉参考；不修改原截图、用户照片、已有30万基线、扫描器、解码器或缓存。没有测量前不宣称降低 CPU 或内存。

## 实施与验证结果

已完成对应 Qt 实现。删除了逐图改变窗口尺寸的函数及两处调用，没有加入窗口尺寸状态或兼容分支。新增控件按窗口创建，未增加线程、预加载、逐图片子控件或持续刷新定时器。工具栏增加按钮留白，路径独占一行；这会占用一些垂直空间，本轮没有据此宣称首屏或资源收益。

| 验证 | 实际结果 | 证据 |
|---|---|---|
| 仅新增操作测试、未修改产品时 | 6个行为失败，init/cleanup通过 | [RED](interaction-validation/interaction-red.txt) |
| 窗口稳定、路径快捷键、三种开图键及鼠标侧键修复后 | 6个行为通过，连同init/cleanup共8项 | [GREEN](interaction-validation/interaction-green.txt) |
| 中间版640宽预览 | 更多按钮被隐藏，回归失败 | [窄窗RED](interaction-validation/compact-preview-red.txt) |
| Qt6.4.2 / Release / Debian12容器 / offscreen | 全量92通过，0失败、0跳过 | [完整输出](interaction-validation/qt64-full.txt) |
| Qt6.9.3 / Release / 本机 / offscreen | 全量92通过，0失败、0跳过 | [完整输出](interaction-validation/qt69-full.txt) |
| Qt6.9.3 / 真实X11桌面 / 静态100% | 定向15通过 | [100%](interaction-validation/xcb-dpi-1.txt) |
| 同桌面 / 静态150% | 定向15通过 | [150%](interaction-validation/xcb-dpi-1.5.txt) |
| 同桌面 / 静态200% | 定向15通过 | [200%](interaction-validation/xcb-dpi-2.txt) |
| 本地HTML / Chrome headless / 1280×900及640宽 | 开图、翻图、排序、缩放、旋转、返回、主题、播放反馈及空态通过，无JS/HTTP错误 | [浏览器记录](interaction-validation/browser.json) |

本机当前只有HDMI-0一台2560×1440显示器，[设备记录](interaction-validation/display.txt)。真实桌面曾出现离屏测试未暴露的窄窗溢出。最终使用简短“图库”入口、合并多余分隔、图标式播放及较小组间距，保留按钮内边距。三个真实缩放档位均记录工具栏所需尺寸596×49，窗口640×440，更多按钮可见；Qt6.4.2离屏为520×45。测试使用1万个纯路径字符串检查长页码，并非扫描1万张图片的性能测试。

定向测试还覆盖主窗口与独立预览各自的Ctrl+L、窗口最大化/恢复后的图库锚点、Ctrl+滚轮、旋转、GIF关闭重开、深色切换及已有DPI像素复用。全量复验发现新增页码标签无有效索引时显示占位字符，违反既有状态清空约定；已改为清空并通过原回归，没有放宽测试。

实际Qt截图：[浅色图库](interaction-native/100/light.png)、[深色图库](interaction-native/100/dark.png)、[预览](interaction-native/100/preview.png)、[空目录](interaction-native/100/empty.png)。截图复用既有benchmark的`--ui`入口和已有1万张目录，仅用于画面核验，没有重跑性能矩阵。样例图为既有壁纸派生数据；HTML仅作本地演示，不新增图片授权声明。

最初并行编译的cc1plus被系统终止，改为串行后构建完成；未修改发布脚本或系统资源设置。桌面存在AT-SPI总线警告，原始文本已保留；没有宣称通过屏幕阅读器验收。内置浏览器连接不可用，网页使用现有Playwright和本机Chrome验证，没有安装新的浏览器或产品依赖。

## 交付范围与边界

- [可操作原型](Interaction.html)：`http://localhost:4311/imageviewer/Interaction.html`，服务仅监听本机。
- 测试构建：`/tmp/iv-ergonomics-build/ImageViewer`；基线提交仍为36f9c16，改动尚未提交、未推送。
- [源码及二进制摘要](interaction-validation/manifest.json)记录实际受测工作区；不能把基线提交直接当作新UI源码提交。
- 原`UI.html`及此前截图保留。原DEB/RPM候选未重建，不能用旧包体验本轮操作。
- 跨屏、动态硬件DPI、原生Wayland、屏幕阅读器、Fedora新UI分发包及远程CI未在本轮验证。三个Qt缩放档位为单屏、分进程QT_SCALE_FACTOR，没有冒充真实跨屏。
- 本轮没有运行ASan/UBSan、数小时长测或规模性能对照；不宣称总体性能、内存、CPU或I/O进一步降低。保留此前缓存与队列的既有回归覆盖。

## 复验入口

```sh
cmake -S . -B /tmp/iv-ergonomics-build -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_PREFIX_PATH=/home/gyl/data/qt/6.9.3/gcc_64 -DBUILD_TESTING=ON
cmake --build /tmp/iv-ergonomics-build --target ImageViewer imageviewer_tests --parallel 1
QT_QPA_PLATFORM=offscreen /tmp/iv-ergonomics-build/imageviewer_tests
QT_QPA_PLATFORM=xcb QT_SCALE_FACTOR=1.5 /tmp/iv-ergonomics-build/imageviewer_tests viewer_keeps_window_geometry directory_shortcut open_selected_with_keyboard viewer_mouse_navigation compact_sort_controls compact_preview_controls desktop_interactions theme_keeps_scene_items dpr_refresh_keeps_image dpr_reuses_existing_pixels relayout_keeps_anchor
```

Qt6.4.2复验使用发布流程已有的Debian12构建器，单路编译并运行既有CTest入口；镜像ID见manifest。测试配置与缓存位于QTemporaryDir，实际截图使用隔离的XDG目录，不覆盖个人设置。日志仅去除行尾空白和末尾空行，保留测量数值及真实失败。
