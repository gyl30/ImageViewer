# ImageViewer

基于 Qt 6 / C++17 的本地瀑布流图片浏览器，使用 Qt Widgets、Graphics View 和视口虚拟化。
支持 JPEG、PNG、WebP、GIF、BMP，提供排序、大图预览、缩放、旋转和回收站删除。

当前版本为内部候选 `0.1.0-rc.1`，尚无正式 Release。**项目许可证未确定，本地验收包不构成再分发授权。**

## Linux 安装

首轮目标为 amd64/x86_64 的 Debian 12、Ubuntu 24.04 和 Fedora 43。
使用发行版共享 Qt 库，不捆绑 Qt 或图片插件。

```sh
# Debian 12 / Ubuntu 24.04
sudo apt install ./imageviewer_0.1.0~rc1-1_amd64.deb

# Fedora 43
sudo dnf install ./imageviewer-0.1.0-0.1.rc1.fc43.x86_64.rpm
```

安装后从应用菜单打开 ImageViewer，或运行 `ImageViewer`。
`Ctrl+O` 打开文件夹，`Ctrl+Shift+O` 打开图片，`F1` 查看快捷键。
当前命令行提供 `--help`、`--version`；尚未提供文件位置参数，因此桌面入口不声明“打开方式”文件关联。

```sh
sudo apt purge imageviewer       # Debian / Ubuntu
sudo dnf remove imageviewer     # Fedora
```

卸载删除系统安装文件，保留个人设置和缓存，不删除图片。

## 从源码构建

工程最低支持 Qt 6.4.2，需要 Core、Gui、Widgets、Concurrent、Svg 模块。
Qt 6.2.4 已实测无法编译现有目录权限接口；未为旧 Qt 添加兼容分支。

```sh
# Debian 12 / Ubuntu 24.04 构建依赖
sudo apt install build-essential cmake ninja-build qt6-base-dev libqt6svg6-dev qt6-image-formats-plugins

cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=OFF
cmake --build build --target ImageViewer --parallel 4
./build/ImageViewer
```

手工安装可在配置时设置 `-DCMAKE_INSTALL_PREFIX=/usr/local`，再执行 `sudo cmake --install build --strip`。
运行回归测试时使用 `-DBUILD_TESTING=ON`，构建 `imageviewer_tests` 后执行 `ctest --test-dir build --output-on-failure`。
测试和 benchmark 不会被安装进正式包。

## 发布与支持范围

- [Linux 构建、打包、安装验收流程](docs/linux-release.md)
- [0.1.0-rc.1 本地安装包验收与依赖清单](docs/releases/0.1.0-rc.1/validation.md)
- [现有功能和资源验收](docs/final-acceptance-2026-10-25.md)

已完成的真实桌面验收范围是 Linux/Xorg 单屏及静态 100%/150%/200% DPI，基于 Qt 6.9.3。
本轮发行包使用系统 Qt，具体版本与包验收结果以发布报告为准，不将旧性能数字外推为新包保证。
真实跨屏、动态硬件 DPI、原生 Wayland、其他架构和其他发行版尚未承诺支持。
大图解码可能产生明显临时内存峰值，64 MiB 缩略图缓存预算不是整个进程的内存上限。

## 许可证

仓库目前没有确定的项目许可证。维护者确认源码、图标和其他资源许可后，才能对外发布。
安装包中的 `copyright` 仅说明当前状态；Qt 和系统依赖沿用发行版各自的许可证。
