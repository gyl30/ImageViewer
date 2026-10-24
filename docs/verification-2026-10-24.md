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
