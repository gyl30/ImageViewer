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
