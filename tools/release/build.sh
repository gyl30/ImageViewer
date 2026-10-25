#!/bin/sh
set -eu

generator=$1
output=$2
case "$generator" in
    DEB) extension=deb ;;
    RPM) extension=rpm ;;
    *) echo "使用方式：build.sh DEB|RPM 输出目录" >&2; exit 2 ;;
esac

source_dir=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
mkdir -p "$output"
output=$(CDPATH= cd -- "$output" && pwd)
build_dir="$output/build"

# 调用者从提交时间设置 SOURCE_DATE_EPOCH，保证复建使用同一时间基准。
: "${SOURCE_DATE_EPOCH:?请设置待构建提交的 SOURCE_DATE_EPOCH}"
cmake -S "$source_dir" -B "$build_dir" -G Ninja \
    -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=/usr -DBUILD_TESTING=ON
cmake --build "$build_dir" --target ImageViewer imageviewer_tests --parallel 4
ctest --test-dir "$build_dir" --output-on-failure
desktop-file-validate "$source_dir/packaging/io.github.gyl30.ImageViewer.desktop"
DESTDIR="$output/stage" cmake --install "$build_dir" --strip
cpack --config "$build_dir/CPackConfig.cmake" -G "$generator" -B "$output"

cmake --version > "$output/cmake-version.txt"
c++ --version > "$output/compiler-version.txt"
ldd "$output/stage/usr/bin/ImageViewer" > "$output/runtime-libraries.txt"
readelf -d "$output/stage/usr/bin/ImageViewer" > "$output/elf-dynamic.txt"
if [ "$generator" = DEB ]; then
    dpkg-query -W > "$output/build-packages.txt"
    dpkg-deb --info "$output"/*.deb > "$output/package-info.txt"
    dpkg-deb --contents "$output"/*.deb > "$output/package-contents.txt"
else
    rpm -qa | sort > "$output/build-packages.txt"
    rpm -qip "$output"/*.rpm > "$output/package-info.txt"
    rpm -qRp "$output"/*.rpm > "$output/package-requires.txt"
    rpm -qlp "$output"/*.rpm > "$output/package-contents.txt"
fi
printf '%s\n' "$SOURCE_DATE_EPOCH" > "$output/source-date-epoch.txt"
(
    cd "$output"
    sha256sum *."$extension" stage/usr/bin/ImageViewer
) > "$output/SHA256SUMS"
