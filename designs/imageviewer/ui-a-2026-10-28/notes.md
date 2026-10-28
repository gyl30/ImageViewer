# 方案A实施与验收

用户已选择A，并要求去掉“更多”及预览左侧显示方式按钮的下拉三角。本页对应正式程序的实际实现；[上一轮A/B对比](../ui-review-2026-10-28/compare.html)保留为历史资料。

## 实际变化

- 主窗口与预览共用20px主题图标、按钮间距、悬停与键盘焦点样式。图标沿用项目SVG；少量符号通过Qt QIconEngine按当前调色板绘制，不增加自建像素缓存或图像依赖。DPI请求仍使用源矢量图。
- 两个“更多”和预览显示方式菜单隐藏三角，菜单本身、提示及快捷键保留。“打开”的文件／目录入口不变。
- 深浅主题统一标题栏、画布与文字颜色。目录短名称并入现有状态文字，比A原型少一个额外标签；不恢复路径输入栏，不新增目录状态字段。
- 卡片悬停使用中性色边框，选中使用蓝色边框，键盘焦点使用内侧虚线。复用时清除焦点；预览切图定位时同步选中与场景焦点，未增加卡片子控件。
- 没有底部图片条、A/B运行选项、常驻动画、新刷新定时器、线程、原图缓存或相邻图预加载。Qt Widgets、C++17、64 MiB缓存和64任务上界保持。

共享标题栏样式集中在window_title_bar，删除两个窗口重复的工具栏样式。图标着色依据[Qt图标引擎接口](https://doc.qt.io/qt-6.8/qiconengine.html)，菜单指示符使用[Qt样式表子控件](https://doc.qt.io/qt-6.5/stylesheet-reference.html)。未复制参考项目的源码、SVG或图片。

## 回归证据

| 检查 | 实际结果 | 记录 |
|---|---|---|
| 采用A前新增回归 | 2通过、6失败：三处三角、目录上下文、图标颜色、卡片焦点 | [RED](evidence/red.txt) |
| 初步实施 | 定向9通过、0失败 | [GREEN](evidence/green.txt) |
| 图标DPI补验 | 原先150%与200%发生重复倍率，像素尺寸30／40而非请求20；显式使用目标像素尺寸修正后，三档定向均7通过 | [150%失败](evidence/icon-1.5.txt)、[200%失败](evidence/icon-2.txt)、[修正后](evidence/dpi-fix-2.txt) |
| 程序定位后的焦点 | 已复现选中项更新、焦点留在旧项；focus_path同步Qt场景焦点后通过 | [RED](evidence/focus-track-red.txt)、[GREEN](evidence/focus-track-green.txt) |
| Qt6.9.3 / GCC11 / Release / offscreen | 118通过、0失败、1跳过 | [全量](evidence/qt69-full.txt) |
| Qt6.4.2 / GCC12 / Debian12 / Release / offscreen | 118通过、0失败、1跳过 | [全量](evidence/qt64-full.txt) |
| 独立虚拟X11 / 100%、150%、200% | 每档41通过、0失败、0跳过 | [100%](evidence/gui-1.txt)、[150%](evidence/gui-1.5.txt)、[200%](evidence/gui-2.txt) |
| 用户build目录 / GCC16 / Release | 构建通过，已更新正式可执行文件 | [构建](evidence/user-build.txt) |

去三角回归比较实际按钮与其无菜单参考图的底部像素，并实际点击确认菜单仍打开；不是只检查样式表字符串。图标测试覆盖三档像素尺寸、深浅切换及启用／禁用颜色。焦点测试覆盖视觉变化、方向键仍滚动、对象复用清理和程序定位。长目录／长文件名的640窗口没有被撑宽。

全量跳过项是仅限真实X11窗口管理器的xcb_header_gestures。焦点GUI测试最初未激活测试窗口而失败，补足激活前提后通过；[原失败记录](evidence/gui-focus-red.txt)保留，没有放宽焦点断言。所有激活、菜单和输入测试均在独立Xvfb内进行，没有再操作用户桌面鼠标。

## 实际Qt截图

[查看器](index.html)可切换以下状态。截图来自正式Qt窗口，没有用HTML模拟应用。

| 浅色图库 | 深色图库 | 预览 | 空目录 | 640图库 | 640预览 |
|---|---|---|---|---|---|
| [查看](screens/light.png) | [查看](screens/dark.png) | [查看](screens/preview.png) | [查看](screens/empty.png) | [查看](screens/narrow.png) | [查看](screens/preview_narrow.png) |

使用已有1万张测试JPEG，不复制图库、不修改原图。空目录由探针临时创建，目录名按实际名称省略显示。

## 资源对照

### 1万张、固定窗口的UI绘制

实施前程序为b98713e布局加上一轮公共缺陷修正，不包含A装饰；实施后为正式A。前后各预热一次、交替运行3个独立进程，100%虚拟X11，图库1100×760、预览1200×800、窄窗640×480。复用同一探针采样协议，每状态整窗绘制60帧、空闲采样500ms；RSS在输出绘制缓冲创建前采样，峰值包含截图和测量开销。

| 指标 | 实施前 | 正式A |
|---|---|---|
| 峰值RSS中位数／范围 | 147.4 MiB／146.6～149.2 | 146.0 MiB／145.7～149.2 |
| 空闲单核CPU中位数 | 0.156% | 0.152% |
| 浅色图库绘制中位数 | 1.906 ms | 1.856 ms |
| 深色图库绘制中位数 | 1.451 ms | 1.641 ms |
| 预览绘制中位数 | 0.333 ms | 0.346 ms |
| 640预览绘制中位数 | 0.186 ms | 0.217 ms |
| 图库／预览QWidget子对象数 | 26／33 | 26／33 |

峰值范围重叠，不声称内存优化。新主题和图标有绘制成本：本次深色整窗合成约多0.190ms，预览约多0.013ms。整窗强制合成不能直接换算真实滚动帧率，也不应把空闲CPU中包含的探针心跳当作正式程序绝对空闲承诺。完整RSS/PSS和阶段数据见[汇总](evidence/measurements.json)及6份ui原始文件。

### 1万、10万、30万规模检查

复用tools/benchmark.py的profile场景：一次扫描、短段连续滚动、三种宽度与清理，前后各运行应用缓存冷／热两种情形。未清内核page cache；cold不代表设备级冷盘。没有重复旧的数小时稳定性矩阵。

| 图片数 | 冷缓存峰值RSS，前→后 | 热缓存峰值RSS，前→后 | 热缓存滚动段CPU，前→后 |
|---|---|---|---|
| 1万 | 141.8→141.9 MiB | 140.2→140.3 MiB | 37.5→37.3 ms |
| 10万 | 162.2→162.3 MiB | 160.9→161.2 MiB | 37.3→40.6 ms |
| 30万 | 208.0→208.1 MiB | 206.8→207.0 MiB | 45.2→44.4 ms |

各规模前后最多29个活动卡片、采样排队最多20个；冷场景解码88次，热场景解码0次、磁盘缓存读取88次。冷场景进程写入约2.02→2.00MB，热场景均为0。滚动段CPU没有随总量放大的新增趋势，GUI心跳P95仍在5～7ms同级，回调至绘制P95上界均为1ms。本次矩阵中没有看到实际浏览路径的明显CPU或I/O回归。

30万在本轮虚拟X11下峰值约208MiB，不能与旧的不同场景197MiB直接相减；同场景前后仅约0.1MiB差异。必需的O(N)元数据继续增长，像素、活动对象和排队不随图库总量增长。扫描先后受OS缓存影响，扫描CPU降低不归因于本次UI改动。12份scale原始JSON保留在evidence目录。

## 复验与边界

```sh
./build/ImageViewer

# 后台截图和UI绘制测量；使用独立的测试配置与缓存目录
xvfb-run -a -s '-screen 0 2560x1600x24' env QT_QPA_PLATFORM=xcb QT_SCALE_FACTOR=1 IV_BENCH_WINDOW=1100x760 XDG_CONFIG_HOME=/tmp/iv-a-config XDG_CACHE_HOME=/tmp/iv-a-cache /path/to/imageviewer_benchmark --ui /path/to/existing/gallery /tmp/iv-a-shots

# 同一规模检查入口
xvfb-run -a -s '-screen 0 2560x1600x24' python3 tools/benchmark.py run --exe /path/to/imageviewer_benchmark --data /tmp/iv-dataset --sizes 10000 100000 300000 --output /tmp/iv-a-scale --backend xcb --scenario profile
```

正式benchmark的--ui已直接支持六种截图、可见按钮检查、RSS/PSS／空闲CPU和整窗绘制采样；不需要候选A/B装饰代码。既有源数据未重新生成。实施前源码差异见[快照补丁](evidence/pre-a-source.patch)，实际受测源码和程序哈希见[manifest](evidence/manifest.json)。验收记录生成时HEAD为b98713e，改动尚未提交、未推送；后续提交与推送状态以Git历史为准，manifest保留测量时快照。原DEB/RPM未重建，发布配置和许可证未修改。

实体跨屏、动态硬件DPI、真实触控板手感、原生Wayland和本次UI的长时间运行未新增设备验证。已有验收证据保留；虚拟X11不会冒充物理桌面验收。
