# Linux 发布流程

目标是生成依赖系统共享库的 DEB/RPM，不引入 AppImage、静态 Qt、额外下载器或自更新服务。
当前候选 `0.1.0-rc.1` 仅供本地验收；许可证未确定，不能据此创建正式 Release 或上传二进制。

## 构建环境

| 产物 | 构建系统 | 系统 Qt | 目标 |
|---|---|---|---|
| DEB | Debian 12 amd64 | 6.4.2 | Debian 12、Ubuntu 24.04 amd64 |
| RPM | Fedora 43 x86_64 | 6.10.3 | Fedora 43 x86_64 |

CMake 最低支持的 Qt 为6.4.2，以实际发行版测试为依据；源码使用了6.3才引入的 `QDir::mkdir` 权限接口，Qt6.2.4已有真实编译失败证据。没有声称未测试的Qt6.3兼容，也不通过删改核心逻辑兼容旧系统。

构建器只安装发行版开发包。Dockerfile固定基础镜像摘要，apt/dnf仍从发行版仓库解析依赖；记录的完整已解析镜像ID及依赖版本用于本轮复建。跨日期重新创建构建器可能获得新依赖，不承诺跨工具链字节一致。
发布输入必须使用Git规范导出，不直接绑定具有不同文件mtime的工作树；RPM会保留较早的资源文件时间。

## 构建与打包

从干净的已提交源码开始，在仓库根执行：

```sh
mkdir -p dist/source
git archive HEAD | tar -x -C dist/source
docker build -t imageviewer-build:debian12 -f dist/source/tools/release/debian.Dockerfile dist/source/tools/release
mkdir -p dist/debian12
SOURCE_EPOCH=$(git show -s --format=%ct HEAD)
docker run --rm --init --user "$(id -u):$(id -g)" \
  -e SOURCE_DATE_EPOCH="$SOURCE_EPOCH" \
  -v "$PWD/dist/source:/src:ro" -v "$PWD/dist/debian12:/out" \
  imageviewer-build:debian12 sh /src/tools/release/build.sh DEB /out
```

RPM使用 `fedora.Dockerfile`、`imageviewer-build:fedora43`、`dist/fedora43` 和参数 `RPM`，其余相同。
每次构建固定 Release、四路编译，仅构建程序和单元测试；不构建 benchmark，不运行大型性能矩阵。
先运行既有回归，再执行 `cmake --install --strip` 和 CPack。

脚本输出安装包、SHA256SUMS、包和暂存可执行文件的字节大小、编译器/CMake版本、完整构建包版本、ELF动态依赖及包内容。
原生RPM后处理可能改变ELF；实际分发程序的大小及SHA256另外在装包后采集，不能用暂存值替代。
包内仅有以下文件和必要目录：

```text
/usr/bin/ImageViewer
/usr/share/applications/io.github.gyl30.ImageViewer.desktop
/usr/share/icons/hicolor/scalable/apps/io.github.gyl30.ImageViewer.svg
/usr/share/doc/imageviewer/copyright
```

测试、benchmark、设计截图、开发文档、Qt库、插件及头文件均不打入包。
图标直接复用仓库SVG，同时设置程序的桌面ID和窗口图标；不改变图片卡片或工具栏。

## 运行时依赖

DEB通过 `dpkg-shlibdeps` 自动记录ELF库依赖，并显式要求SVG图标引擎、WebP插件及hicolor主题。
xcb和offscreen平台插件由自动依赖的libqt6gui6提供，不额外安装其他设备平台的qt6-qpa-plugins集合。
RPM保留rpmbuild自动依赖发现，并显式要求相应Qt插件包。
这些插件通过运行时加载，不能只检查主程序的 `ldd`。
构建于Qt6.10.3的RPM可能具有Qt_6.10符号要求，不能仅看工程最低Qt版本推断它可在旧Fedora运行。

发行版软件包粒度使 `qt6-image-formats-plugins` 等可能同时安装其他格式插件，程序仍只声明现有五格式。
安装包本身不带私有Qt、绝对开发机RPATH或QtTest。
桌面入口不传递程序尚未处理的文件参数，也不修改用户的默认图片关联。

## 干净环境验收

使用原始系统镜像创建容器，不复用构建器。下面示例中的包名应与实际输出一致：

```sh
mkdir -p dist/runtime-debian12
docker run --rm --init \
  -v "$PWD/dist/source:/src:ro" -v "$PWD/dist/debian12:/packages:ro" \
  -v "$PWD/dist/runtime-debian12:/out" debian:12-slim \
  sh /src/tools/release/verify.sh /packages/imageviewer_0.1.0~rc1-1_amd64.deb /out
```

同一个DEB另在Ubuntu24.04验证；RPM在Fedora43验证。CI使用固定摘要的镜像。
验收先记录基础包，再仅安装应用及依赖并记录版本/安装大小，最后安装GUI测试工具，避免把测试工具当成应用依赖。

脚本检查实际包清单和库加载，以普通用户从已安装的desktop入口启动程序。
在Xvfb/X11与Openbox下通过真实文件对话框打开五种128×96测试图，检查加载完成标题、窗口图标及Qt实际加载的xcb/SVG/JPEG/WebP/GIF插件。
随后打开测试目录，等待五个缩略图写入，正常关闭并使用原生包管理器卸载，检查全部安装文件已删除。
个人配置和缓存按桌面程序惯例保留；卸载前后逐文件核对配置、缓存及可写测试图片的SHA256。
测试输入是仓库自生成的小型色块的独立副本，不读取或改写用户原图。

这证明分发包在干净用户空间中的X11功能，不等同于真实多屏、Wayland或新一轮数小时稳定性验收。

## 可重复构建

使用同一已解析构建器、同一提交的两份独立Git导出、同一 `SOURCE_DATE_EPOCH`，在两个全新容器和输出目录分别执行完整构建。
比较两个安装包及安装后可执行文件的SHA256，而非仅比较文件大小。
DEB由CPack使用SOURCE_DATE_EPOCH规范时间；RPM固定构建主机标记、构建时间，并使用Fedora43的RPM时间策略。
首次RPM复打包已发现默认构建主机/时间导致哈希不同；进一步跨导出复验又发现工作树mtime差异。
规范Git导出固定内容、模式和文件时间，因此不引入额外的时间修补脚本。

```sh
cd dist/debian12
sha256sum -c SHA256SUMS
```

正式候选应保留源码提交、SOURCE_DATE_EPOCH、构建器ID和包版本清单。
不同发行版、编译器或仓库更新后的二进制不同，不属于本轮字节一致性承诺。

## GitHub Actions

`linux-packages.yml` 在main推送、PR及手工触发时，用两个发行版构建器执行回归、双次构建比对和干净环境安装验收。
仅需仓库读取权限，checkout固定提交，凭据不持久化。
许可证未确定，因此没有artifact上传、写标签、签名或创建Release步骤。
本地等价命令通过不能冒充远程Actions已经运行；未推送时需将远程运行列为待验。

## 候选与正式发布

DEB版本为 `0.1.0~rc1-1`，RPM为 `0.1.0-0.1.rc1.fc43`，均先于对应正式版本排序。
本轮只交付本地、未签名候选，SHA256提供一致性检查，不是发布者身份认证。
正式发布前必须确认许可证及资源权属，更新包内许可说明，完成远程CI并审阅本轮安装验收报告。
然后再由维护者决定签名、标签、二进制上传与正式Release；本流程不会自动执行。

参考：[Qt6.9 QDir接口](https://doc.qt.io/archives/qt-6.9/qdir.html)、[CPack DEB](https://cmake.org/cmake/help/v3.25/cpack_gen/deb.html)、[CPack RPM](https://cmake.org/cmake/help/v3.31/cpack_gen/rpm.html)。RPM时间宏同时以构建器内 `/usr/lib/rpm/macros` 为核验依据。
