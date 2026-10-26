# 0.1.0-rc.1 运行依赖

包使用发行版共享库，不携带 Qt。以下为本次干净安装的实测结果，测试工具在应用依赖采样之后才安装。

## DEB 直接依赖

从最终 [DEB control](debian12/package-info.txt)读取：

```text
libqt6core6 (>= 6.4.2)
libqt6svg6 (>= 6.4.2)
qt6-image-formats-plugins
hicolor-icon-theme
libc6 (>= 2.34)
libgcc-s1 (>= 3.0)
libqt6core6 (>= 6.4.0)
libqt6gui6 (>= 6.4.0)
libqt6widgets6 (>= 6.3.0)
libstdc++6 (>= 11)
```

其中较严格的 Core 下限来自工程支持范围，较宽松的一条由 dpkg-shlibdeps 自动生成；两条约束同时生效，不代表两个 Core 副本。xcb/offscreen 平台插件由 libqt6gui6 提供，不额外依赖设备平台插件合集。SVG 和 WebP 的插件包显式要求，不能仅靠主程序 ldd 推断插件齐全。

Ubuntu24.04 将部分库改用 `t64` 包名；原生 apt 已实测完成上述依赖解析，实际版本见 [Ubuntu 应用包清单](runtime-ubuntu24/application-packages.tsv)。没有另行重建 Ubuntu 包。

## RPM 直接依赖

显式依赖为：

```text
qt6-qtbase-gui >= 6.4.2
qt6-qtsvg >= 6.4.2
qt6-qtimageformats
hicolor-icon-theme
```

原生 RPM 另外自动发现 Qt Core/Gui/Widgets/Concurrent/Svg、OpenGL、glibc、libstdc++ 及对应符号约束；完整原始列表见 [package-requires.txt](fedora43/package-requires.txt)。其中 `libQt6Core.so.6(Qt_6.10)(64bit)` 是本次构建产物的真实要求，不可将 CMake 的6.4.2下限解读为该 RPM 能在旧 Qt 上运行。

## 共享依赖占用

| 官方基础系统 | 新增包数（含应用） | 应用与共享依赖声明增量 MiB | 应用自身声明字节 | 仅共享依赖声明增量 MiB |
|---|---:|---:|---:|---:|
| Debian12 slim | 103 | 266.27 | 418816 | 265.87 |
| Ubuntu24.04 | 101 | 276.55 | 418816 | 276.15 |
| Fedora43 | 148 | 475.60 | 387404 | 475.23 |

计算为应用安装后软件包声明大小之和减去基础系统之和。此次没有基础包升级。Debian/Ubuntu 输入单位为 Installed-Size 的 KiB，乘1024；RPM 为 Size 字节。数据不含后来安装的 Xvfb/Openbox/操作工具，**不是进程内存、实际文件系统分配或下载量**。系统 Qt GUI 的依赖还包含图形、字体、输入及格式支持；已有桌面可能已经安装其中大部分，不能把最小容器增量外推成每台设备的额外占用。

完整解析记录：

- Debian：[基础](runtime-debian12/base-packages.tsv)、[应用安装后](runtime-debian12/application-packages.tsv)、[新增闭包](runtime-debian12/application-added-packages.tsv)、[汇总](runtime-debian12/summary.json)。
- Ubuntu：[基础](runtime-ubuntu24/base-packages.tsv)、[应用安装后](runtime-ubuntu24/application-packages.tsv)、[新增闭包](runtime-ubuntu24/application-added-packages.tsv)、[汇总](runtime-ubuntu24/summary.json)。
- Fedora：[基础](runtime-fedora43/base-packages.tsv)、[应用安装后](runtime-fedora43/application-packages.tsv)、[新增闭包](runtime-fedora43/application-added-packages.tsv)、[汇总](runtime-fedora43/summary.json)。

`runtime-packages.txt` 另包含后装测试工具，仅用于重现验收环境，不用于上表计算。两个构建目录的 build-packages.txt 则是开发包环境，也不属于分发依赖。

## 运行加载核验

主 ELF 在安装测试工具前执行 ldd，无缺失库、QtTest 或开发机路径。实际窗口运行后从 `/proc/PID/maps` 读取插件路径，确认系统 xcb、SVG 图标引擎、JPEG、GIF、WebP 已加载；PNG/BMP 通过开图验证。发行版插件集合中其他模块被探测加载，不意味着本项目扩大了格式支持。

证据：[Debian ldd](runtime-debian12/ldd.txt) / [插件](runtime-debian12/gui/loaded-plugins.txt)、[Ubuntu ldd](runtime-ubuntu24/ldd.txt) / [插件](runtime-ubuntu24/gui/loaded-plugins.txt)、[Fedora ldd](runtime-fedora43/ldd.txt) / [插件](runtime-fedora43/gui/loaded-plugins.txt)。
