# 第一阶段整改与验证记录

环境：Linux、Qt 6.9.3、GCC 16.2.0。构建目录 `/tmp/imageviewer-build`，GUI 测试使用 `QT_QPA_PLATFORM=offscreen`，配置和缓存隔离到临时目录。

| 问题 | 修复前实际失败 | 修复后验证 |
| --- | --- | --- |
| C1 | `imageviewer_tests large_visible_batch`：submissions 实际 5，预期 1；测试限制重入深度，避免栈溢出 | 同命令通过，一次提交的 240 张缩略图全部收到；应用与测试编译通过，`git diff --check` 通过 |
| C2 | `imageviewer_tests queued_cache_clear`：queued clear_cache 后 marker.png 仍存在 | 同命令及 C1 回归通过；每个工作事件解码一项，空闲时返回事件循环，删除条件变量和永久循环 |
| C3 | `relayout_preserves_image`：普通重排像素变灰；追加批次时旧项仍使用旧宽度 | 两个数据用例和对象池重用测试通过；常态像素宽度按 scale×pixmap.width 检查，排除 Qt 选择框边距；累计 CTest 通过 |
| C4 | `latest_layout_width`：同宽重复后 840 错停在 640，440→640→440 错停在 640 | 四个序列通过，另含 future 已结束但通知未消费、布局中清空换目录；累计 CTest 通过 |
| C5 | `preloads_after_reopen`：重开后无法完成左右两个邻居的预加载 | 换一组未缓存图片重开后，两个邻居依次完成；累计 CTest 通过 |
| C6 | `stale_queued_scans`：连续三个 queued 请求产生三个 finished，预期仅一个 | 仅最新请求发出 batch/finished；扫描会话直接取自 scanner 原子代次，累计 CTest 通过 |
| C7 | `exif_orientation`：5–8 方向元数据为 120×60 而非 60×120；先修元数据后再次观察到缩略图 40×20 而非 40×80 | 1–8 方向的元数据、目标尺寸和四象限颜色全部通过；磁盘缓存版本更新，避免复用旧错误缩略图；累计 CTest 通过 |
| C8a | `thumbnail_file_replaced`：覆盖为蓝图后仍返回红像素 | 同进程重新请求及新 loader 的磁盘缓存读取均为蓝色；解码与存盘固定使用开始时的版本 key，累计 CTest 通过 |
| C8b | `viewer_file_replaced`：主加载缓存和预加载缓存两条路径覆盖后均仍显示旧图 | 两条路径重新打开均显示新像素，关闭重开预加载回归仍通过；累计 CTest 通过 |
| C9 | `movie_stops_on_close`：close 后 QMovie 仍为 Running | 关闭后停止并销毁，重开恢复播放，切换静态图后旧 movie 销毁；累计 CTest 通过 |
| C10 | `tall_image_stays_visible`：长图覆盖当前视口却不可见 | 使用既有索引空间保存前缀最大底边，二分只查询已布局部分；滚动后长图保留、离屏短图回收，累计 CTest 通过 |
| C11 | `item_geometry`：放大后左上角为 (-39,-4)，缩小后为 (109.5,69.5)，预期均为 (10,20) | 基础缩放使用 item transform，hover 保持原有居中放大；放大/缩小的像素边界及 hover 恢复均通过，删除冗余 base_scale_；累计 CTest 通过 |
| C12 | `panorama_is_requested`：完整和增量布局都不提交全景图请求 | 两处高度下限设为 1；两条路径均发出请求，加载后图项可见、实际像素边界高度不小于 1；累计测试与 sanitizer 通过 |

补充公共边界验证：真实主窗口拖放连续切换三个目录，仅显示最后目录并加载两张缩略图；30 次排队翻图最终只显示最后一张；静态图任务未完成时切换 GIF，旧任务不覆盖动画；加载中关闭并重开；9000×8000 JPEG 按内存限制缩小读取，并在关闭/析构时清理；取消缩略图后可重新提交；501 张图片的扫描在首批回调取消后不发完成信号。

累计 Qt Test：35 个结果通过（含数据行、初始化和清理），CTest 1/1 通过，约 3.1 秒。测试固定 `en_US` locale；首次集成测试发现系统 C locale 下 Qt QCollator 不执行数字排序，属于原有环境限制，未修改产品排序行为。offscreen 的 propagateSizeHints 警告不影响断言。

## 最终验证

普通构建使用 GCC 16.2.0，应用和测试均编译成功。最终 CTest 1/1 通过，Qt Test 35 passed / 0 failed / 0 skipped，用时 3.09 秒。

```bash
cmake -S . -B /tmp/imageviewer-build -DCMAKE_PREFIX_PATH=/home/gyl/data/qt/6.9.3/gcc_64
cmake --build /tmp/imageviewer-build -j 4
ctest --test-dir /tmp/imageviewer-build --output-on-failure
```

Clang 23.0.0 编译项目源码并启用 AddressSanitizer、UndefinedBehaviorSanitizer。最终 CTest 1/1 通过，35 个结果全部通过，用时 3.27 秒；启用了泄漏检测，未报告 sanitizer 错误或泄漏。

```bash
cmake -S . -B /tmp/imageviewer-sanitize -DCMAKE_PREFIX_PATH=/home/gyl/data/qt/6.9.3/gcc_64 -DCMAKE_CXX_COMPILER=clang++ -DCMAKE_CXX_FLAGS='-fsanitize=address,undefined -fno-omit-frame-pointer' -DCMAKE_EXE_LINKER_FLAGS=-fsanitize=address,undefined
cmake --build /tmp/imageviewer-sanitize -j 4
ASAN_OPTIONS=detect_leaks=1:halt_on_error=1 UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1 ctest --test-dir /tmp/imageviewer-sanitize --output-on-failure
```

测试通过公共接口、Qt 信号及 QObject 子对象核验，没有提升 private 可见性。源码差异通过 `git diff --check aba91da`。
上述 sanitizer 范围为本项目构建的源码；Qt 使用已有的未插桩动态库。未运行 TSan、跨平台测试或真实桌面人工操作，不据此宣称所有并发交错、格式和平台均已验证。

## 规范复核（Standards）

只读独立复核未发现缺少控制语句大括号、private 可见性提升、全局格式化或多余转发框架。对于函数参数较多及测试文件增大，仅列为启发式观察，未要求新增抽象。

复核曾认为 `stop_scan` 单行函数违反 Allman。主线程实际执行 `clang-format --style=file --dump-config`，配置为 `AllowShortFunctionsOnASingleLine: All`；将该函数送入 clang-format 后输出不变。因此此项不成立，不为此增加格式改动。

最终确认：规范轴 0 项未解决的硬性违反。

## 需求复核（Spec）

九个重点方向已核实并建立回归，其中缓存分为缩略图和原图/预加载两项；补充修复长图漏绘、缩放位置和全景图零高度。13 项修复均有修复前实际失败记录，并分别提交。

复核曾认为 C10 的顶部截断查询依赖错误的单调性。主线程重新阅读完整和增量算法：每次取所有列的最小高度 y，随后仅将该列增加正高度及间距；其他列高度原本均不小于 y，因此下一个最小值不小于 y。顶部坐标单调不减，查询可在第一个超过底边的位置结束。前缀最大底边负责找到跨行长图的最早候选，两者职责不同，未据该疑点增加扫描或状态。

复核时 C12 尚处于未提交验证阶段，现已补齐 RED/GREEN、实际加载后的图项可见性与像素边界断言，并提交。清空会清除所有 active 索引，增量仅追加模型，完整结果还有 generation 校验；未证实所提 active 索引越界假设，不增加边界兜底。

最终确认：需求轴 0 项未完成的已确认整改；未证实的生命周期/并发假设与已知响应性限制单列如下。

## 保留风险与下一阶段

- 文件版本使用路径、mtime 毫秒和大小；保留相同版本信息的覆写不能识别。文件在一次请求进行中被外部修改也不保证本次显示原子切换；后续请求会重新计算版本。
- 关闭会等待当前解码；快速翻图中不再监听的值捕获任务可能继续完成。未发现本轮测试中的 UAF 或泄漏，但慢盘关闭延迟和峰值内存仍需测量。
- GIF 的压缩大小不能直接约束 CacheAll 的解码内存；下一阶段测量长动画，再决定缓存策略。
- 扫描排序阶段的 std::sort 仍需完成后才能响应取消；元数据读取已逐项检查代次。下一阶段先测大目录的取消延迟。
- C locale 的数字排序限制、主窗口关闭时是否同时关闭独立查看器，留待明确产品行为后处理。
- 先测滚动帧耗时、活动图项数量、磁盘缓存目录枚举和落盘清理耗时、冷暖缓存加载时间及快速切换峰值内存，再做性能优化。
- 交互阶段优先验证重排滚动锚点、选中项稳定性、失败缩略图反馈、关闭行为；本轮没有视觉风格重构。

## 提交记录

基线 `aba91da6826d54a8e7565601eb15b38162a49144`，分支 `main`。未推送 GitHub。

| 提交 | 对应交付 |
| --- | --- |
| `baa7332` | 先提交当前源码审查报告 |
| `0dccec9` | C1：删除队列溢出的同步重投回路 |
| `ef75f83` | C2：恢复 worker 事件循环 |
| `18ee03e` | C3：重排保留缩略图并更新旧项几何 |
| `8e25bd1` | C4：完整布局消费最新宽度 |
| `a0d7761` | C5：关闭后重新建立预加载观察器 |
| `e8d87a3` | C6：扫描代次过滤过期请求 |
| `83aef49` | C7：EXIF 方向和缩放尺寸 |
| `9dfd9e4` | C8a：缩略图缓存文件版本 |
| `d266361` | C8b：原图和预加载缓存文件版本 |
| `4833526` | C9：关闭时停止并释放 GIF |
| `c475431` | C10：跨行长图正确保留 |
| `6ab534d` | C11：基础缩放对齐布局矩形 |
| `208212d` | 跨模块、异步生命周期及大图补充测试 |
| `e919e9a` | C12：完整/增量布局中的全景图最小高度 |

最后另以文档提交保存本节结项记录；13 个行为修复均保持独立提交。
