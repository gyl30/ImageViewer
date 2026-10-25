# 真实环境最终验收

在当前 Linux / Qt6.9.3 / 单屏范围内，可以进入发布准备。两小时最终版本长测、真实素材功能及三档静态DPI复验通过；没有发现仍可复现的阻断性正确性问题。真实跨屏和动态显示器DPI仍未验证，不能宣传为全面多屏验收通过。一般图库内存保持原有水平；仅高DPI下的无效放大得到明确改善，平滑绘制有可测成本。

## 版本与复核边界

起点 `6fd0ef5`，main，工作区干净。已阅读 resource-review、resource-results、前轮 acceptance-results、benchmark和相关测试；不重做此前50个整改提交已有充分证据的项目。origin/main还包含此前16个本地差距，因此起点领先66个提交，不将该计数误写为本轮工作量。未拉取、未推送。

最终受测产品为 `7dbcffd`，Release / GCC16.2 / Qt6.9.3，Linux6.8、i7-13700KF、NVMe/ext4。可执行文件、探针及runner摘要见 [long/run.json](benchmarks/final-real-2026-10-25/long/run.json)，正式启动时跟踪文件无修改；后续DPI、基线和I/O复验使用同一可执行文件。后续9fa587f和2a88961只调整测试及离线统计，未改变受测产品或长测驱动逻辑。前轮30万约197MiB结论只适用于其派生JPEG轨迹，不是任意大图的内存上限。

本轮产品改动限于首次回收站数据目录创建、缩略图原生尺寸上限及卡片平滑绘制。没有增加产品配置、线程、缓存层、预加载、数据库或通用调度框架。新增采样、操作循环和统计仅在既有benchmark中；长测样本流式写JSONL，不在进程内累积完整记录。未发现需另作整改的重复产品状态或废弃兼容路径。

## A：真实素材与只读核验——通过

用户提供 `/home/gyl/fs/tmp/download`，实际为 `/home/gyl/fk/fs/tmp/download`。2332个文件中1304个扩展名匹配现有图片格式：1303个有效头（598 JPEG、705 PNG），1个异常PNG。候选内容约1.80GiB；最大约79MP，JPEG高度可达65500，PNG高度可达19972。EXIF方向6真实样本2张；没有原生WebP/GIF。称为真实素材库，不声称全部都是独立相机原片。[初始摘要](benchmarks/final-real-2026-10-25/source-before.json)。

原图未进入修改、覆盖或删除调用。开始和结束对1304个候选逐文件计算SHA256并核对size、mtime和权限，新增/删除/变化均为0；前后核验记录摘要相同。读取引发的正常atime变化不等同于内容修改。名称明细只在私有临时目录，不导出照片内容、GPS或私人文件名。[最终核验](benchmarks/final-real-2026-10-25/source-after.json)。

补充集约2.61MiB：两个独立临时副本、派生WebP/GIF各一个、异常和无权限文件各一个；覆盖中文、空格、引号、#、%、[]、+及深目录。派生格式不冒充真实原生来源，不增加格式依赖。删除仅针对独立副本；受控大图多路径也只链接临时副本，未给原图建立硬链接。

最终真实xcb复验：100%/150%/200%各1303模型、6种排序（名称/时间/大小正倒序）、15张代表图预览，共45次成功、0失败；包括两张EXIF旋转图及像素极值，检查解码宽高比、旋转/适宽、关闭和副本回收站删除。补充集4个有效模型，4次预览及6种排序通过，坏头和不可读文件被跳过。[100%](benchmarks/final-real-2026-10-25/photos-dpi-1.json)、[150%](benchmarks/final-real-2026-10-25/photos-dpi-1.5.json)、[200%](benchmarks/final-real-2026-10-25/photos-dpi-2.json)、[补充集](benchmarks/final-real-2026-10-25/supplement.json)。部分JPEG有EOI前多余字节警告但成功解码，未修补或重编码原图。

## 两项已确认问题与修复

| 问题与位置 | 触发、根因、严重度 | 最小修复与验证 |
|---|---|---|
| 首次回收站删除失败，main_window.cc:723 | Linux XDG_DATA_HOME尚不存在时，Qt6.9.3回收站返回EBADF；首次使用的功能缺陷，中等 | 删除前创建数据根，新建0700，不改变已有权限，仍交给QFile；应用级真实RED/GREEN，验证副本、原文件、场景及权限 |
| 缩略图无效放大，image_loader.cc:213/234；waterfall_item.cc:142 | 小图或窄长图在高DPI下被放大解码和缓存，没有增加细节；自绘未使用已有平滑模式。资源和画质缺陷，中等 | 解码/后处理不超过原生尺寸；自绘尊重已有变换模式；缓存版本4排除旧放大结果，不保留旧读取路径；五格式尺寸与混色像素回归真实RED/GREEN |

第一项独立Qt实验也确认缺失/存在目录的差异。Qt6.9.3[实现](https://raw.githubusercontent.com/qt/qtbase/v6.9.3/src/corelib/io/qfilesystemengine_unix.cpp)只创建Trash子目录，[XDG规范](https://specifications.freedesktop.org/basedir/latest/)允许首次写入创建缺失目录。因此不以隔离测试环境为由忽略首次使用错误。[删除RED](benchmarks/final-real-2026-10-25/trash-red.txt)、[GREEN](benchmarks/final-real-2026-10-25/trash-green.txt)。

第二项在真实图库200% usage轨迹对照：应用缓存首次/重复峰值RSS从489.5/477.3降至414.6/400.9MiB；CPU为16.98/16.56→16.68/16.81秒，进程写入131.62/131.37→134.42/134.56MiB。返回数173/184→185/174，不能把异步单次差异当作统计显著总体提速。请求窗口1280×800，200%受窗口管理器约束，前后末态均960×511；不是整个过程都保持1280×800。源页缓存已热，首次仅表示各自应用缓存为空。[前测](benchmarks/final-real-2026-10-25/native-before)、[后测](benchmarks/final-real-2026-10-25/native-after)、[RED](benchmarks/final-real-2026-10-25/native-thumb-red.txt)、[GREEN](benchmarks/final-real-2026-10-25/native-thumb-green.txt)。旧磁盘文件仍由现有总预算淘汰，没有另加清理层。

## B：显示器与DPI——单屏静态通过，真实跨屏未验证

Xorg/GNOME只有HDMI-0连接，2560×1440@60Hz，其他输出未连接。[设备记录](benchmarks/final-real-2026-10-25/display.txt)。本轮三档均为真实xcb窗口、分进程QT_SCALE_FACTOR；每档桌面交互及现有五格式测试8项通过，覆盖锚点、窗口变化和预览操作。[100%](benchmarks/final-real-2026-10-25/dpi-tests-1.txt)、[150%](benchmarks/final-real-2026-10-25/dpi-tests-1.5.txt)、[200%](benchmarks/final-real-2026-10-25/dpi-tests-2.txt)。此前主题和原生截图验收仍有效，未重复视觉重构。

跨屏移动、真实异DPI迁移、显示器DPI动态改变均未验证。受控DPI事件测试和上述静态结果只能证明代码路径，不能冒充硬件迁移；不改全局桌面缩放设置，也不声称人工触控板或合成器逐帧验收。

## C：两小时稳定性——通过，保留预览收尾波动

最终版本从12:45:39.635运行至14:45:39.779（+08:00），实际7200.144秒，退出0。完成194轮、23286滚动步、3104随机跳转、582预览、194切目录；覆盖真实素材/既有30万JPEG库、1000/1400窗口变化、缩放旋转、GIF和原生Qt事件循环空闲。每10轮清图、清缓存后重开。持续操作按真实事件推进，没有长sleep凑测试时间。

原3894c82的约71分钟前测因发现放大缺陷而中止，保留为 [未完成前测](benchmarks/final-real-2026-10-25/long-before-native-thumb/summary.json)，complete=false，不计入两小时通过，也不与完整后测直接比较累计I/O。

1035个阶段样本，排除预先规定的前20分钟后，按阶段、模型数及20分钟分桶。空闲段RSS/PSS中位数如下（MiB）：

| 分钟 | 真实1303张 RSS / PSS | 30万张 RSS / PSS |
|---|---:|---:|
| 20–40 | 224.3 / 195.4 | 244.8 / 214.6 |
| 40–60 | 222.6 / 192.6 | 236.9 / 208.2 |
| 60–80 | 225.3 / 196.4 | 245.7 / 218.4 |
| 80–100 | 219.7 / 189.7 | 240.1 / 211.6 |
| 100–120 | 209.5 / 181.9 | 231.7 / 203.5 |

空闲段RSS拟合斜率分别−9.1/−10.6MiB/h；预热后清理点−24.0MiB/h。检查了全部19次周期清理及终态，未发现持续增长。不能隐藏另一组正斜率：真实库的“预览关闭60ms”样本约+30.6MiB/h，20分钟桶中位数为287.0、277.2、300.3、286.8、341.1MiB，存在明显收尾波动；随后空闲和清理回落，没有持续到稳态。该组不是同一解码时刻，不能仅据混合收尾样本判泄漏，也不将其写成所有阶段均下降。[完整分桶、趋势与全部清理点](benchmarks/final-real-2026-10-25/long/summary.json)。结论只覆盖本次两小时。

整程Peak RSS804.7MiB，采样PSS峰值548.4MiB；包含真实79MP解码临时像素，不能同197MiB普通JPEG基线混比。终态RSS94.8/PSS64.5MiB；模型、活动图、像素缓存、磁盘缩略图及队列均0，保留40个不持有图片的空池项。线程15–16、FD31–35，无累计增长；活动项最大30。30秒阶段快照队列峰值13，探针内部观察最大22，产品硬上限64。所有快照QImage缓存均≤64MiB。逻辑图片字节可能共享，不能相加当RSS。

CPU共1450.87秒，约单核20.15%：loader879.42秒、扫描245.14秒，其余326.31秒含GUI、布局、Qt及探针，不能当纯布局热点。10秒空闲段CPU中位约单核0.0365%。major faults0、minor faults31611564；这是本次已热页缓存长测，不证明冷启动无缺页。进程read_bytes3.34MiB、write_bytes5959.02MiB，rchar约163.12GiB；大量页缓存读取和系统调用不等同于读盘。syscr63469477、syscw1794825。

114223个缩略图返回，112571可见、838预取、814过期、0失败。队列/worker至回调/提交至绘制p95为122/37/143ms，p99为297/88/307ms；回调至绘制p95为1ms。这里只测Qt绘制事件，不是屏幕呈现帧时间。长测未启用16ms GUI心跳，相关计数0，不伪造长测GUI延迟百分位；独立短usage测量见D。

正式日志与统计约2.4MB，stderr6256字节、未截断；监控启动成功，结束时由runner主动终止（监控rc−15不等于被测程序失败）。90秒/32秒工具冒烟和取消回收回归仍单独保留，不冒充两小时：[流程](benchmarks/final-real-2026-10-25/long-tool-smoke.json)、[空闲截止](benchmarks/final-real-2026-10-25/idle-tool-smoke.json)、[取消](benchmarks/final-real-2026-10-25/cancel-tool-test.json)。

## D：I/O——应用计数通过，独占设备/NAND归因未验证

原库位于nvme0n1p2，缓存/临时目录位于nvme0n1p5，同一物理NVMe。冷组仅对支持的图片提出FADV_DONTNEED建议，未清全系统页缓存；mincore只作有限采样，不泛称全库驻留率。热组沿用同一应用缓存，仍可能因128MiB预算淘汰或不同访问目标读取源文件。

最终7dbcffd同usage、xcb/1280×800/100%：

| 打开 | CPU秒 | Peak RSS MiB | 进程读取 / 写入 MiB | 源分区读取 MiB | 缓存分区完成写入 MiB |
|---|---:|---:|---:|---:|---:|
| 冷 | 13.49 | 392.6 | 617.18 / 77.92 | 617.18 | 83.63 |
| 重复 | 12.11 | 293.6 | 406.27 / 50.64 | 406.27 | 118.52 |

[最终记录](benchmarks/final-real-2026-10-25/io-final)。工具前测为CPU13.86/11.45秒、进程读取638.65/396.67MiB、写入80.32/51.96MiB，峰值365.1/301.3MiB；访问完成数和分配时序不同，未证明总体CPU、I/O或稳态内存进一步下降，也不能从设备列反推应用回退。[前测](benchmarks/final-real-2026-10-25/io)。最终短usage GUI定时器延迟p95冷/重复均6ms，提交至绘制p95为255/49ms，包含冷读盘差异。

最终有界热工作集profile（12个连续位置加重排）重复时13次全部磁盘命中，0源解码、0进程读取、0写入；首次13次解码。这里源页缓存已热，不将这个小轨迹替代完整浏览体验。[最终热工作集](benchmarks/final-real-2026-10-25/hot-final)。

快速滚动写入诊断沿用修复前100%下的strace证据，只累计.thumb成功write/pwrite/writev：快速71.48MiB/4945次，逐步等待237.24MiB/15965次；返回422/1308，可见145/1237，过期139/1，源解码334/802，两组未解析行均0。完成量和访问图片不同，不把快速少完成当优化，也不把单个不可中断解码的过期写入扩大成新调度框架需求。strace数据不用作正常CPU/延迟基准。[原始诊断](benchmarks/final-real-2026-10-25/write-trace)。

两小时iostat按30秒速率积分，源p2读约4.41MiB、写0.42MiB，缓存p5读1330.54MiB、写10801.72MiB；同一物理设备平均util约1.97%，最大11.69%。设备比进程多出的读写包含其他进程、文件系统元数据与延迟回写，不能称为1.8倍应用写放大，更不是NAND写入。长测期间未并行本轮其他重负载测试；长测后的30秒背景抽样仍有约101KiB/s可读进程写入，且430个进程行无权限读取，不能扣除到此前两小时。原图payload完整性由逐文件核验确认。[设备汇总](benchmarks/final-real-2026-10-25/long/device-summary.json)、[iostat](benchmarks/final-real-2026-10-25/long/iostat.txt)、[pidstat](benchmarks/final-real-2026-10-25/long/pidstat.txt)、[背景抽样](benchmarks/final-real-2026-10-25/background-io.json)。未格式化、清分区、改perf权限或停止用户其他进程。

## 与原规模基线的同场景复验

复用原/tmp/iv-dataset，64个派生JPEG画面及既有1万/10万/30万目录项，不重新复制大库。修复前后可执行文件用同一Qt及探针，offscreen/1280×800/DPR1、3×120步、额外360帧，按相同顺序各首次/重复运行。与真实xcb照片和长测不能混比。

| 数量 | Peak RSS MiB 前→后，首次/重复 | CPU秒 前→后，首次/重复 | 360帧绘制ms 前→后，首次/重复 |
|---|---|---|---|
| 1万 | 125.5/125.4 → 125.7/126.1 | 11.17/11.41 → 12.73/11.53 | 536/575 → 959/717 |
| 10万 | 152.3/152.9 → 152.8/152.6 | 11.98/11.93 → 12.55/12.31 | 540/547 → 768/703 |
| 30万 | 196.6/197.0 → 196.8/197.4 | 13.90/14.06 → 14.27/14.06 | 567/569 → 760/728 |

[前](benchmarks/final-real-2026-10-25/legacy-before)、[后](benchmarks/final-real-2026-10-25/legacy-after)。一般规模内存没有进一步下降，仍保持10万约153MiB、30万约197MiB；没有把单次小差异当成收益。平滑绘制增加约0.4～1.2ms/显式帧（1万首次有较大波动），后测约2.0～2.7ms/帧；总CPU有部分上升，30万重复仍14.06秒。不能声称UI零开销。该成本用于保留原生小图后的平滑显示，短usage未显示明显GUI响应恶化，现阶段接受，不再为消除小成本增加缓存或绘制机制。

30万长测模型的结构容量约19.45MiB、路径内容约25.75MiB，是随N增长的必要元数据，尚不含所有字符串头/分配器。64MiB QImage预算及由视口/宽高比决定的活动像素与N分开；真实长图可能使单个活动图本身很大，不能把“活动项有界”误写成全部像素固定64MiB。

## 大图边界与资源回收

独立真实79MP JPEG临时副本预览：首图452ms、关闭13ms、Peak RSS538.2MiB；关闭/销毁后RSS约79.4/66.1MiB。派生双帧GIF运行3秒，CacheAll=false，销毁后66.7MiB；不冒充此前120帧GIF或数小时动画压力测试。探针阶段名large_and_neighbors沿用旧记录名称，产品没有相邻原图预加载。[独立预览](benchmarks/final-real-2026-10-25/real-viewer.json)。

79MP样本为1242×63588。Qt6.9.3 JPEG[实现](https://raw.githubusercontent.com/qt/qtbase/v6.9.3/src/plugins/imageformats/jpeg/qjpeghandler.cpp)在原生M/8输出与请求尺寸不同时会再缩放，当前请求1086×55639涉及取整边界。直接向上取整可能选择更大解码比例，因此没有凭峰值改变清晰度策略或引入解码框架。独立538MiB与图库同用805MiB证据表明峰值是实际边界，但未做完整堆归因，不把全部额外内存归因到某一次缩放。

该临时峰值和关闭后允许不可中断解码收尾属于当前接受边界；稳态、清理回落和两小时无累计增长已分别验证。不可缩小格式仍受Qt分配限制，未为固定缩略图预算拒绝本次大图需求。

## 测试、提交与后续边界

Release全量84项通过；三档真实xcb定向各8项；离线统计脚本2项通过。两项产品修复都有真实RED/GREEN；定向ASan/UBSan/泄漏检查删除3项、原生尺寸/平滑8项通过（均含init/cleanup），没有宣称全量sanitizer。[最终全量](benchmarks/final-real-2026-10-25/release-tests-final.txt)、[统计](benchmarks/final-real-2026-10-25/analysis-tests.txt)、[删除sanitizer](benchmarks/final-real-2026-10-25/trash-sanitizer.txt)、[尺寸sanitizer](benchmarks/final-real-2026-10-25/native-thumb-sanitizer.txt)。

最终复核发现新增格式测试没有沿用既有插件缺失跳过约定：隔离Qt插件目录时实际出现3个失败，修复后4项通过、3项正确跳过；正常安装的最终全量仍84通过、0跳过。另将重复的20分钟分析单位统一命名，原两小时分析输出逐字节相同，没有新增配置。[插件RED](benchmarks/final-real-2026-10-25/pluginless-red.txt)、[GREEN](benchmarks/final-real-2026-10-25/pluginless-green.txt)、[分析复验](benchmarks/final-real-2026-10-25/analysis-constant-tests.txt)、[两轴审查记录](benchmarks/final-real-2026-10-25/review.md)。

独立本地提交：

- `383633b` 修复首次创建数据目录时的回收站删除。
- `3894c82` 扩展真实图库与按操作推进的长稳验收工具。
- `8eb37f4` 增加长稳结果分桶与趋势分析回归。
- `7dbcffd` 避免缩略图超过原生尺寸并保留平滑显示。
- `9fa587f` 按现有约定跳过缺失格式插件的测试。
- `2a88961` 统一长稳分析时间分桶常量，统计结果不变。
- 本报告及完整测量证据单独提交；不推送。

| 项目 | 结果 |
|---|---|
| 真实素材扫描/排序/预览/副本删除/源文件完整性 | 通过 |
| 单屏100%/150%/200%、既有锚点与交互回归 | 通过 |
| 最终Release两小时持续操作与资源回落 | 通过；仅覆盖本时长与素材 |
| 应用冷/热I/O、磁盘命中、快速滚动写入分析 | 通过；未证明需要新增缓存机制 |
| 真实跨屏、动态硬件DPI、独占设备及NAND写入归因 | 未验证 |
| 仍可复现的本轮产品缺陷 | 无；两项已修复并回归 |

保留小型私有复验输入及既有30万基线，私有临时目录总占用小于20MiB；成功运行的临时缓存自动删除，本轮旧隔离缓存和回收站副本已清理。仓库本轮全部证据约5MiB，不包含照片。下一步只需在第二显示器和更多原生摄影格式到位后补硬件与素材覆盖；若需验证4小时，应另跑4小时，不将本次2小时外推。当前无需继续架构优化或视觉改造，可进入当前支持范围的发布准备。

## 重现入口

使用已有Release benchmark及已有数据；fixtures为包含有效小图、GIF/WebP、复杂路径和异常文件的小型补充目录。先阅读原图只读约束，photos模式只删除自己创建的QTemporaryDir副本。

```sh
python tools/benchmark.py long --exe /tmp/iv-release/imageviewer_benchmark --directory /home/gyl/fs/tmp/download --alternate /tmp/iv-dataset/300000 --fixtures /tmp/iv-final-real/fixtures --output /tmp/iv-repeat-long --seconds 7200
python tools/benchmark.py analyze-long --input /tmp/iv-repeat-long/timeline.jsonl --output /tmp/iv-repeat-long/summary.json
python tools/benchmark.py run --exe /tmp/iv-release/imageviewer_benchmark --data /tmp/iv-dataset --sizes 10000 100000 300000 --output /tmp/iv-repeat-legacy
python tools/benchmark.py run --exe /tmp/iv-release/imageviewer_benchmark --directory /home/gyl/fs/tmp/download --output /tmp/iv-repeat-io --backend xcb --scenario usage --evict-files
python -m unittest discover -s tests -p test_benchmark_analysis.py
```

源页缓存建议驱逐只用于该测试目录，不保证全系统冷态；正式设备汇总按iostat每行rkB/s、wkB/s分别乘30秒求和，再除1024得到MiB。命令文本仅去除行尾空白及末尾空行，不改测量值。JSONL中保留全部原始字段和实际UTC，足以另选预热期或分桶，不需要第二套测试框架。
