# 大图库基准与整改记录

## 方法

Linux / Qt 6.9.3 / GCC 16.2.0 / Release / offscreen / DPR=1，窗口 1280×800。基线实现为 `a85a683`，未做资源或 UI 优化。采样工具及数据生成器见 tools/benchmark.py 和 tools/performance_probe.cc；完整数值见 benchmarks/baseline/*.json。

```bash
python3 tools/benchmark.py generate --root /tmp/iv-dataset --source /home/gyl/Pictures/壁纸/Bliss.jpeg
cmake -S . -B /tmp/iv-release -DCMAKE_BUILD_TYPE=Release -DCMAKE_PREFIX_PATH=/home/gyl/data/qt/6.9.3/gcc_64
cmake --build /tmp/iv-release -j 4
python3 tools/benchmark.py run --exe /tmp/iv-release/imageviewer_benchmark --data /tmp/iv-dataset --output docs/benchmarks/baseline
```

每规模一个空应用缓存的新进程，随后保留磁盘缩略图再用新进程重复打开；第二次结束测试清理缓存。操作系统页缓存未清空。64 个真实壁纸派生混合尺寸 JPEG，以硬链接生成独立目录项；适合比较枚举/排序/元数据/缓存开销，不能代表 30 万张不同原图的冷盘 I/O。

浏览轨迹为三个来回、每段 120 个均匀位置，每步 40ms，显式调用现有 check_visible_area 来比较相同视口需求，另外渲染 360 帧；随后三个宽度重排、返回顶部和清理。连续滚动的 150ms 防抖响应单独讨论，不把漏刷当作性能优势。首个缩略图与首个实际可见缩略图分开记录。单次矩阵不是统计显著性试验。

RSS/PSS/HWM 与进程 CPU、proc I/O 为实际采样；元数据容量/UTF-16 路径和 cache/pixmap 为逻辑字节估计，共享图像不能直接相加当作物理内存。队列峰值是提交回调采样值；loader CPU 为工作线程 CPU 时钟累计，包含解码、存盘和缓存维护。

## 基线结论

- 10 万张长滚动及重排后 RSS 约 310–315 MiB，30 万张约 379 MiB；首次扫描约 1.0s / 3.2s。
- 活动项峰值 85，和 N 无关；队列采样峰值约 167，当前没有硬上限。
- 缩略图 QCache 填满约 200 MiB；扫描规模的 O(N) 元数据与固定像素缓存必须分开。
- 三段浏览中 loader CPU 占主要部分：首次打开一轮约 18s 工作线程 CPU，重复打开约 15s；约 2000 张磁盘缓存下逐图枚举清理是已确认热路径。
- 空闲 500ms 仅约 0.2–0.6ms CPU，无常驻忙循环。
- 清空后逻辑图片缓存为 0，但 RSS 不一定下降，allocator 保留、场景 vector 容量和累计 loaded_paths_ 仍在；不能单凭 RSS 认定泄漏。
- Release 下上轮 35 个结果继续通过。后续新回归已经观察到：失败缩略图没有完成信号，三次待解码切换提交三个任务；四种焦点导航键均通过，不改未复现行为。

基线完整矩阵已保存；下一阶段以同一数据和轨迹比较，不增加解码线程数。
