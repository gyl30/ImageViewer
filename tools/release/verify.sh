#!/bin/sh
set -eu

package=$1
output=$2
source_dir=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
mkdir -p "$output"
output=$(CDPATH= cd -- "$output" && pwd)

case "$package" in
    *.deb)
        apt-get update
        dpkg-query -W -f='${binary:Package}\t${Version}\t${Installed-Size}\n' > "$output/base-packages.tsv"
        apt-get install -y --no-install-recommends "$package"
        dpkg-query -W -f='${binary:Package}\t${Version}\t${Installed-Size}\n' > "$output/application-packages.tsv"
        ldd /usr/bin/ImageViewer > "$output/ldd.txt"
        apt-get install -y --no-install-recommends \
            xvfb xauth xdotool openbox libglib2.0-bin dbus-x11 x11-utils passwd util-linux diffutils desktop-file-utils
        dpkg-query -L imageviewer > "$output/owned-paths.txt"
        dpkg-query -W > "$output/runtime-packages.txt"
        ;;
    *.rpm)
        rpm -qa --qf '%{NAME}.%{ARCH}\t%{EVR}\t%{SIZE}\n' > "$output/base-packages.tsv"
        dnf install -y --setopt=install_weak_deps=False "$package"
        rpm -qa --qf '%{NAME}.%{ARCH}\t%{EVR}\t%{SIZE}\n' > "$output/application-packages.tsv"
        ldd /usr/bin/ImageViewer > "$output/ldd.txt"
        dnf install -y --setopt=install_weak_deps=False \
            xorg-x11-server-Xvfb xauth xdotool openbox glib2 dbus-daemon xprop shadow-utils util-linux diffutils desktop-file-utils
        rpm -ql imageviewer > "$output/owned-paths.txt"
        rpm -qa | sort > "$output/runtime-packages.txt"
        ;;
    *) echo "仅验证DEB/RPM" >&2; exit 2 ;;
esac

while IFS= read -r path; do
    if [ -f "$path" ]; then
        printf '%s\n' "$path"
    fi
done < "$output/owned-paths.txt" | LC_ALL=C sort > "$output/installed-files.txt"
cat > "$output/expected-files.txt" <<'EOF'
/usr/bin/ImageViewer
/usr/share/applications/io.github.gyl30.ImageViewer.desktop
/usr/share/doc/imageviewer/copyright
/usr/share/icons/hicolor/scalable/apps/io.github.gyl30.ImageViewer.svg
EOF
cmp "$output/expected-files.txt" "$output/installed-files.txt"
if grep -q 'not found\|Qt6Test\|/home/' "$output/ldd.txt"; then
    exit 1
fi
desktop-file-validate /usr/share/applications/io.github.gyl30.ImageViewer.desktop
QT_QPA_PLATFORM=offscreen ImageViewer --version > "$output/version.txt"
if ! getent passwd 1000 > /dev/null; then
    useradd --uid 1000 --create-home imageviewer-test
fi
test_user=$(getent passwd 1000 | cut -d: -f1)
mkdir -p "$output/gui"
chown 1000 "$output/gui"
runuser -u "$test_user" -- dbus-run-session -- xvfb-run -a sh "$source_dir/tools/release/gui-smoke.sh" \
    "$source_dir/tools/release/fixtures" "$output/gui"

case "$package" in
    *.deb) apt-get purge -y imageviewer ;;
    *.rpm) dnf remove -y imageviewer ;;
esac
while IFS= read -r path; do
    test ! -e "$path"
done < "$output/expected-files.txt"
printf 'install, desktop launch, five formats, gallery and uninstall: PASS\n' > "$output/result.txt"
