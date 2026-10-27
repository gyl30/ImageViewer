# 自定义标题栏与图标操作

按本轮明确要求，主窗口和预览窗口均使用自定义标题栏，操作与窗口控制合并为一行。移除常驻路径栏、路径输入字段及对应快捷键，没有保留隐藏的旧入口。

## 当前行为

- 顶部按钮只显示SVG图标，悬停提示与可访问名称保留操作含义；菜单条目保留文字。
- 左上角入口名为“打开”，点击选择文件夹，下拉提供“打开图片”；原有Ctrl+O和Ctrl+Shift+O保留。
- “最近”直接列出目录和文件名称，取消“最近文件夹／最近图片”二级分类，不显示完整路径。沿用现有历史存储：目录记录在前、图片记录在后，各组内保留最近顺序，没有新增时间字段或历史模型。
- 排序位于“更多 → 排序”，保留文件名、修改时间、文件大小与倒序；顶部不再显示当前排序项。
- 标题栏空白区拖动，双击最大化／还原；右侧提供最小化、最大化／还原、关闭，窗口边缘与四角可调整大小。全屏时禁用最大化按钮，保留已有F11与Esc逻辑。
- 预览保留图标式返回图库、翻图、显示方式、缩放、右转、幻灯片及页码；切图不改变窗口大小。

主窗口与预览复用`window_title_bar`，只负责窗口控制与边缘输入，不涉及图片解码或缓存。移动和缩放调用Qt的[系统窗口接口](https://doc.qt.io/qt-6/qwindow.html#startSystemMove)，没有手动维护拖动坐标、动画、额外定时器或平台回退实现。删除了预览创建时重复设置`Qt::Window`的代码，避免清掉无边框标志；QMainWindow本身已设置该标志，见[Qt构造说明](https://doc.qt.io/qt-6/qmainwindow.html#QMainWindow)。最低Qt6.4.2通过实际构建与功能回归验证。

## 验证

| 项目 | 实际结果 | 记录 |
|---|---|---|
| 产品修改前，新增自定义标题栏／平铺历史／排序入口回归 | 4个行为失败，init/cleanup通过 | [RED](interaction-v2-validation/red.txt) |
| 初步实现后，同一定向回归 | 6项通过，0失败 | [GREEN](interaction-v2-validation/green.txt) |
| Qt6.4.2 / GCC12 / Release / Debian12容器 / offscreen | 96通过，0失败，1项跳过 | [全量](interaction-v2-validation/qt64-full.txt) |
| Qt6.9.3 / GCC11 / Release / 本机 / offscreen | 96通过，0失败，1项跳过 | [全量](interaction-v2-validation/qt69-full.txt) |
| 本机X11 / 100% | 20通过，0失败、0跳过 | [100%](interaction-v2-validation/xcb-1.txt) |
| 本机X11 / 150% | 20通过，0失败、0跳过 | [150%](interaction-v2-validation/xcb-1.5.txt) |
| 本机X11 / 200% | 20通过，0失败、0跳过 | [200%](interaction-v2-validation/xcb-2.txt) |
| 用户的build目录 / GCC16 / Release | ImageViewer构建通过，已更新可执行文件 | [构建](interaction-v2-validation/user-build.txt) |
| 本地Chrome原型 | 路径移除、历史平铺、排序、图标操作、640宽布局等通过，无JS/HTTP错误 | [浏览器](interaction-v2-validation/browser.json) |

离屏跳过项仅为`xcb_header_gestures`，不能据此声称离屏验证了系统拖动。它在三个真实X11缩放档位执行：使用系统XTest输入移动标题栏，再拖动右下角，检查移动／尺寸增加的方向与逻辑像素上界，并检查移入内容后光标恢复。测试库动态加载且仅用于测试，不增加程序运行依赖。

原生手势的补验曾失败，保留[150%尺寸失败](interaction-v2-validation/native-input-failure.txt)、[窗口响应超时](interaction-v2-validation/native-input-timeout.txt)与[200%少移动一步](interaction-v2-validation/native-input-start-failure.txt)的原始记录。驱动原来假设系统即时接管按下事件；改为探测指针抓取后，[组合验证](interaction-v2-validation/native-combined-failure.txt)与[单独验证](interaction-v2-validation/native-grab-probe-failure.txt)仍有失败，已删除这种会主动抓取指针的探测。

分阶段测量仍出现窗口几何事件滞后的偏差，保留[记录](interaction-v2-validation/native-measurement-failure.txt)。最终删除抓取探测与分阶段测量，使用一次持续拖动，等待实际几何变化。原先新加的“全部输入位移等于窗口位移±3”断言改为：窗口确实向拖动方向移动、宽高确实增加，且增量不超过请求的60×40逻辑像素（上界允许3像素误差）。这仅证明系统操作生效，不证明接管阶段的逐像素一致性；不能把早期严格断言的失败写成产品修复。没有修改产品窗口操作代码来补偿测试，产品没有增加延时、重试或手动坐标补偿。

回归同时检查两个窗口的图标按钮、最大化／还原、双击标题栏、最小化和关闭；以及菜单数据、排序执行、键盘开图、鼠标侧键、图库锚点、主题、GIF关闭重开和原有DPI像素复用。最近菜单改为名称展示时仍将真实位置用于打开，不修改历史中的文件。

最终预览工具栏内容所需尺寸为456×43逻辑像素，连同窗口控制在640×440窗口中可见。实际截图：[浅色图库](interaction-v2-native/100/light.png)、[深色图库](interaction-v2-native/100/dark.png)、[预览](interaction-v2-native/100/preview.png)、[空目录](interaction-v2-native/100/empty.png)。复用既有benchmark的`--ui`入口和既有1万张目录，仅作截图，没有新建大图库或重跑规模性能矩阵。

本机编译大测试文件时曾出现cc1plus被系统终止，保留[GCC16记录](interaction-v2-validation/compiler-terminated-gcc16.txt)和[GCC11记录](interaction-v2-validation/compiler-terminated-gcc11.txt)。停止并行开展构建后，GCC11串行重编译通过；没有为此修改系统资源设置或项目编译选项。用户程序仍由原有GCC16编译。AT-SPI总线不可用警告保留，屏幕阅读器未验证。

## 使用与范围

```sh
./build/ImageViewer
```

- [第二版交互原型](Interaction%20v2.html)：`http://localhost:4311/imageviewer/Interaction%20v2.html`。网页窗口控制为浏览器内示意，系统拖动和缩放以真实Qt验证为准。
- [源码、二进制和环境摘要](interaction-v2-validation/manifest.json)记录验证时的工作区快照：基线为36f9c16，当时两轮UI改动尚未提交。后续提交与推送状态以Git记录为准。
- 前一版设计及记录保留作历史参考；没有修改其测试数据。旧DEB/RPM未重建，不能用旧包检查本轮UI。
- 本机仍为单台HDMI-0显示器。三档验证使用分进程QT_SCALE_FACTOR；真实跨屏、动态硬件DPI、原生Wayland与其他设备没有在本轮验证。
- 未运行ASan/UBSan、长时间稳定性或资源对照，不宣称内存、CPU或I/O进一步下降。扫描、布局、像素缓存与队列逻辑未修改，瀑布流仅同步空态提示文字。

复验可构建既有`imageviewer_tests`，以offscreen运行全量，或在真实X11桌面运行`custom_header recent_menu_is_flat sorting_is_in_more xcb_header_gestures compact_sort_controls compact_preview_controls desktop_interactions`。原生手势测试会短暂操作测试窗口和鼠标，结束后恢复指针位置。
