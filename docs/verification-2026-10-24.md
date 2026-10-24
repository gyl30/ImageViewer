# 第一阶段整改与验证记录

环境：Linux、Qt 6.9.3、GCC 16.2.0。构建目录 `/tmp/imageviewer-build`，GUI 测试使用 `QT_QPA_PLATFORM=offscreen`，配置和缓存隔离到临时目录。

| 问题 | 修复前实际失败 | 修复后验证 |
| --- | --- | --- |
| C1 | `imageviewer_tests large_visible_batch`：submissions 实际 5，预期 1；测试限制重入深度，避免栈溢出 | 同命令通过，一次提交的 240 张缩略图全部收到；应用与测试编译通过，`git diff --check` 通过 |
