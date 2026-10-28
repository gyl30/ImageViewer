# ImageViewer 交互审查与 Qt 候选方案

基线：b98713e，开始时main工作区干净。本轮保留Qt Widgets、C++17、现有瀑布流虚拟化与对象池、64 MiB缩略图缓存和64任务边界。未修改发布包或许可证配置，未提交、未推送。

目前完成公共缺陷修正和A/B评审候选，尚未选择正式UI。可打开[截图比较器](compare.html)切换浅色、深色、预览、空目录、640px图库和640px预览。全部图片来自实际Qt窗口，不是HTML绘制的应用界面。

## 参考项目对比

2026-10-28查阅以下官方资料。GitHub连接器的README端点拒绝访问，改用公开仓库页面、原始文档与官方站点。未安装运行这五个程序，不比较它们的实际速度或资源消耗，也没有引入其源码、SVG或视觉素材。

| 项目与依据 | 图库、浏览与预览 | 导航、状态和键鼠 | 本项目的取舍 |
|---|---|---|---|
| [FlowVision](https://github.com/netdcy/FlowVision/blob/main/README_zh.md) | 自适应瀑布流、深浅主题，双击切换图片视图 | Finder式文件操作，鼠标长按／滚轮与右键手势 | 借鉴图片为主的布局；保留现有键位，不新增长按、手势体系或macOS窗口控制 |
| [qView](https://interversehq.com/qview/) | 尽量只保留图片与标题栏，操作放在上下文菜单 | 文件历史、旋转等能力按需出现 | 借鉴画面面积与低密度；保留图标和提示保证可发现性，不引入其预加载机制 |
| [Gwenview官方手册](https://github.com/KDE/gwenview/blob/master/doc/index.docbook)与[应用页](https://apps.kde.org/gwenview/) | Browse/View分工，目录与操作上下文清楚 | 可收起缩略图条、侧栏信息、Ctrl+滚轮缩放 | 借鉴模式分工与按需导航；不引入KDE依赖、编辑器或常驻侧栏。手册有较早版本内容，不把它等同当前二进制实测 |
| [PicView](https://github.com/Ruben2776/PicView/blob/dev/.github/README.md) | 可隐藏的界面、深浅主题与底部图库 | 可关闭图库、键盘选图、按需图片信息和标题栏文件操作 | 借鉴可选图片条与轻量信息，不改变现有快捷键，不引入额外原图缓存、预加载或标题栏编辑文件功能 |
| [PhotoQt](https://github.com/luspi/photoqt/blob/master/README.md) | 简洁主界面与按需元信息 | 键鼠快捷操作、排序与图片信息 | 借鉴信息按需呈现；不迁移QML，不引入地图、插件或复杂浮层。官网被访问保护阻挡，细节仅按可访问的官方仓库说明核实 |

这是交互思路的比较，不是五个项目的兼容性或性能评测。保留本项目的自定义标题栏、图标按钮、无常驻路径栏和已有深浅主题选择。

## 当前实际 Qt 审查

先看b98713e的真实截图，再用同一Qt代码在独立X11显示重拍；没有以旧HTML原型替代现状。用户要求后台运行后，后续Qt截图、GUI测试和资源测量均用xvfb-run，不激活当前桌面，也不操作当前鼠标。

| 项目 | 证据与位置 | 判断与处理 |
|---|---|---|
| 深色标题栏 | 原样式在window_title_bar及两个toolbar重复指定palette背景；[修正前截图](screens/baseline-before/dark.png)和像素回归均显示实际#efefef、期望#17191e。`window_title_bar.cc:23`、`main_window.cc:157`、`image_viewer_window.cc:158` | 已确认缺陷。删除标题栏背景／边框样式，改为Qt调色板填充；工具栏透明，主窗口和已存在的预览一起跟随主题。无重排、无新主题状态。[修正后](screens/baseline/dark.png) |
| 图标可发现性 | `main_window.cc:79`、`image_viewer_window.cc:126`：18px、固定SVG描边色#8090a6；在#efefef平面背景上的名义对比度约2.83:1 | 有提示和F1入口，但浅色下偏淡。候选改20px与主题文字色，保留图标按钮；不把名义色值计算当作完整可访问性认证 |
| 目录上下文 | `main_window.cc:478`保存当前目录，但`main_window.cc:645`只展示数量与扫描耗时 | 确认信息缺口。A放状态行，B放顶部短目录名；均不恢复完整路径输入栏 |
| 选中、悬停、焦点 | `waterfall_item.cc:23`仅设置可选择；`waterfall_item.cc:132`忽略样式选项；`:169`的悬停和选中均用Highlight，仅线宽不同 | 确认状态区分不足，不是崩溃问题。本轮候选沿用原卡片绘制；选型后细化悬停／选择／键盘焦点，保留原键位，不添加逐卡片子控件 |
| 两窗口一致性 | 共享window_title_bar、SVG及状态信息；原主题缓存导致主窗口与后创建的预览呈现不一致 | 公共主题修正已覆盖两个窗口由浅→深→浅。候选统一图标和背景，未加入动画、阴影或新的常驻面板 |
| 窄窗口／DPI | 原640宽已有测试；本轮补做两套640图库和预览，检查所有标题栏按钮在窗口内 | 本轮三档缩放在独立虚拟X11中验证，不能替代物理跨屏或动态硬件DPI验收 |
| 滚轮、触控板、键盘、侧键 | `image_viewer_window.cc:803`的Ctrl分支把非正纵向角度一律当作缩小 | 已复现横向、零增量和仅像素正向滚动的问题。仅有效纵向增量缩放；优先角度、角度为空才用像素。保留1.2倍步进、普通滚动、键盘和鼠标侧键，不声称真实触控板手感已经验收 |
| 预览操作与状态 | `image_viewer_window.cc:126`的返回／导航／显示方式／缩放／旋转／播放；`:536`与`:624`提供图片信息和缩放比例 | 基本分组合理，切图窗口稳定。A保留；B增加可收起邻近小图。查看640预览可直接比较84px导航高度的代价 |

## 两个候选及成本

| 比较项 | A：极简图库 | B：增强导航 |
|---|---|---|
| 目标 | 图片面积优先，优先建议 | 连续选片、回看与视觉定位优先 |
| 目录 | 状态行显示省略后的短名称 | 顶部显示短名称，底部继续展示图片信息 |
| 预览 | 现有紧凑操作组，不增加图片条 | 更多菜单控制图片条；展开时占84px逻辑高度 |
| 对象 | 图库仅多1个标签；预览保持33个QWidget子对象 | 图库同样多1个标签；预览43个QWidget子对象，比基线多10个，其中7个为缩略图按钮 |
| 像素 | 不新增图片像素容器 | 最多7张64×48逻辑像素图标，取自图库已有小图；未加载时占位，没有新增解码／预加载请求。自身保留像素理论上界约84 KiB（100%），不等同整个Qt进程的RSS增量 |
| 关闭与释放 | 保留原预览关闭流程 | 隐藏时清空7个图标；本轮检查关闭后图标均为空、清目录后活动卡片为0 |
| 复杂度 | 低：现有窗口的图标、上下文和状态细化 | 中：还要处理图片条与目录快照、快速跳转、隐藏／重开、DPI及可见图像同步 |

候选代码位于本目录的candidate_widgets.cc和独立CMake工程。根项目CMake未引用它们，正式程序中没有A/B切换配置或两套UI分支。

B不是可直接合入的成品：当前原型从现有路径快照定位当前图片，并响应场景更新；`indexOf`具有线性查找成本。它没有永久保存整库图片对象，但后段随机导航时的刷新代价尚未测量，不能把首图场景推断为30万图片通过。选B后应先验证实际瓶颈，再利用已有索引和像素完成正式集成，避免增加通用调度或缓存层。

### 实际截图

| 状态 | A | B |
|---|---|---|
| 浅色图库 | [查看](screens/a/light.png) | [查看](screens/b/light.png) |
| 深色图库 | [查看](screens/a/dark.png) | [查看](screens/b/dark.png) |
| 大图预览 | [查看](screens/a/preview.png) | [查看](screens/b/preview.png) |
| 空目录 | [查看](screens/a/empty.png) | [查看](screens/b/empty.png) |
| 640px图库 | [查看](screens/a/narrow.png) | [查看](screens/b/narrow.png) |
| 640px预览 | [查看](screens/a/preview-narrow.png) | [查看](screens/b/preview-narrow.png) |

使用既有1万张派生JPEG，图库目录只创建一个指向既有目录的符号链接，未复制图片库，也未修改原图。空目录为临时测试目录。截图不产生新的图片素材授权声明。

## 验证与实测

| 验证 | 结果 | 证据 |
|---|---|---|
| 滚轮新增回归，修正前 | 9通过、7失败 | [RED](evidence/wheel-red.txt) |
| 同一滚轮回归，修正后 | 16通过、0失败（含init/cleanup） | [GREEN](evidence/wheel-green.txt) |
| 两窗口主题像素回归，修正前 | 2通过、1失败 | [RED](evidence/palette-red.txt) |
| 两项公共修正定向回归 | 17通过、0失败 | [GREEN](evidence/common-green.txt) |
| Qt6.4.2 / GCC12 / Debian12容器 / Release / offscreen | 111通过、0失败、1跳过 | [全量](evidence/qt64-full.txt) |
| Qt6.9.3 / GCC11 / 本机 / Release / offscreen | 111通过、0失败、1跳过 | [全量](evidence/qt69-full.txt) |
| Qt6.9.3 / 独立虚拟X11 / 静态100%、150%、200% | 每档34通过、0失败、0跳过 | [100%](evidence/gui-1.txt)、[150%](evidence/gui-1.5.txt)、[200%](evidence/gui-2.txt) |
| 用户build目录 / GCC16 / Release | 正式ImageViewer构建成功，包含两项公共修正，不含A/B原型 | [构建](evidence/user-build.txt) |
| A/B窗口结构 | 宽／窄窗口标题栏按钮全部可见，B图片条固定7控件，关闭清空图标 | [原始测量](evidence/metrics-b-3.json) |

全量离屏跳过项仅xcb_header_gestures。虚拟X11定向组没有把窗口管理器拖动测试混入；本轮未重做物理桌面拖动、跨屏、动态DPI或实体触控板。窗口状态、关闭、键鼠、主题、布局锚点和DPI像素复用由本次定向／全量测试覆盖相应代码路径。AT-SPI告警保留在原始stderr。

### 同场景资源比较

所有数值来自同一候选可执行文件的baseline/A/B模式。baseline保留b98713e布局，但已包含两项公共修正；严格原始b98713e只用于缺陷复现和修正前截图，未混作新布局性能数据。

复用tools/performance_probe.cc的扫描、等待、采样和渲染入口，构建时生成只用于本次比较的探针入口；没有建立第二套图片加载器或长期benchmark框架。1万张、同一目录、同一100%虚拟X11、图库1100×760／预览1200×800／窄窗640×480。先预热应用缓存，再按轮转顺序各跑3次独立进程，每状态强制绘制整个窗口60帧，空闲CPU采样500ms。RSS/PSS在强制渲染缓冲创建前采样；进程峰值包含截图和绘制基准本身。未隔离其他桌面进程，数据不代表GPU呈现、真实滚动帧率或磁盘冷缓存性能。

| 方案 | 峰值RSS中位数（范围） | 空闲单核CPU中位数 | 深色图库绘制中位数 | 预览绘制中位数 | 640预览绘制中位数 |
|---|---|---|---|---|---|
| 基线布局＋公共修正 | 146.0 MiB（145.9～149.7） | 0.176% | 1.575 ms | 0.329 ms | 0.187 ms |
| A | 148.9 MiB（146.4～149.9） | 0.194% | 1.666 ms | 0.354 ms | 0.216 ms |
| B | 146.4 MiB（146.3～146.6） | 0.154% | 1.664 ms | 0.416 ms | 0.270 ms |

A没有显示资源下降，峰值中位数高约3 MiB，范围与基线重叠；不能据3次采样认定这是稳定差值。B虽RSS中位数接近基线，但增加10个预览控件，预览绘制约多0.087 ms，不能只看内存数字就说它更便宜。PSS、各阶段RSS、读写量、CPU与线程等字段见[测量汇总](evidence/measurements.json)及9份对应metrics原始文件。

本轮该场景宽图库活动卡片最多22个，640图库6个；清空后0个。缓存预算始终64 MiB，稳态采样任务为0，64任务边界另由原有回归验证。没有增加常驻动画、刷新定时器、线程、原图缓存或相邻原图预加载。

空闲CPU包含既有探针心跳与测试事件循环，不是正式程序绝对空闲CPU的承诺。这些数据仅用于候选成本比较，不足以宣布10万／30万性能不回退，也未运行新的长时间稳定性测试。既有规模和资源验收记录保留；选定UI后再做必要的大规模同场景检查，重点看新增绘制与B刷新路径，不机械重复整个旧矩阵。

## 复验入口与后续边界

```sh
cmake -S designs/imageviewer/ui-review-2026-10-28 -B /tmp/iv-ui-review-build -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_PREFIX_PATH=/path/to/Qt/gcc_64
cmake --build /tmp/iv-ui-review-build --parallel 1
python3 designs/imageviewer/ui-review-2026-10-28/run_candidates.py --binary /tmp/iv-ui-review-build/imageviewer_ui_review --directory /path/to/existing/gallery --output /tmp/iv-ui-review-runs --repeats 3
```

runner总是在独立Xvfb中运行，不使用当前桌面显示；raw metrics及measurements.json保存在output目录。根项目原有imageviewer_tests仍是正式回归入口。受测源码、程序哈希和环境见[manifest](evidence/manifest.json)。

建议先选A，原因是面积与维护复杂度，不是虚构的资源收益。若更重视连续选片则可选B，接受84px导航高度与额外刷新验证。正式落地只保留选中的一套，不在生产程序加入A/B配置。选型后再完成卡片状态、目录上下文、图标主题反馈及对应绘制／规模回归；不恢复路径栏，不改既有键位、缓存结构、发布包或许可证。
