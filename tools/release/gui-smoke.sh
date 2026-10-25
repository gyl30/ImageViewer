#!/bin/sh
set -eu

fixtures=$1
output=$2
mkdir -p "$output"
export XDG_CONFIG_HOME="$output/config"
export XDG_CACHE_HOME="$output/cache"
export XDG_DATA_HOME="$output/data"
export QT_QPA_PLATFORM=xcb

# WM客户端列表不会递归查询正在创建或销毁的Qt子窗口。
wait_window()
{
    pattern=$1
    for attempt in $(seq 1 150); do
        window=$(wmctrl -l | while read -r id desktop host title; do
            case "$title" in
                $pattern) printf '%s\n' "$id"; break ;;
            esac
        done)
        if [ -n "$window" ]; then
            printf '%s\n' "$window"
            return 0
        fi
        sleep 0.1
    done
    wmctrl -l >&2
    return 1
}

openbox > "$output/window-manager.txt" 2>&1 &
manager_pid=$!
trap 'kill "$manager_pid" 2>/dev/null || true' EXIT
timeout 10 sh -c 'until xprop -root _NET_SUPPORTED | grep -q _NET_ACTIVE_WINDOW; do sleep 0.1; done'

gio launch /usr/share/applications/io.github.gyl30.ImageViewer.desktop \
    > "$output/application.txt" 2>&1
main_window=$(wait_window 'ImageViewer')
app_pid=$(xdotool getwindowpid "$main_window")
xprop -id "$main_window" WM_CLASS > "$output/window-class.txt"
xprop -id "$main_window" _NET_WM_ICON > "$output/window-icon.txt"
grep -q 'Icon (' "$output/window-icon.txt"

for format in png jpg webp gif bmp; do
    xdotool windowactivate --sync "$main_window"
    xdotool windowfocus --sync "$main_window"
    xdotool key --clearmodifiers ctrl+shift+o
    dialog_window=$(wait_window 'Select Image')
    xdotool windowactivate --sync "$dialog_window"
    xdotool windowfocus --sync "$dialog_window"
    xdotool key --clearmodifiers ctrl+l
    xdotool type --clearmodifiers --delay 1 "$fixtures/color.$format"
    xdotool key --clearmodifiers Return
    image_window=$(wait_window "Viewer - color.$format (128x96)*")
    xdotool getwindowname "$image_window" >> "$output/opened-images.txt"
    xdotool windowactivate --sync "$image_window"
    xdotool windowfocus --sync "$image_window"
    xdotool key --clearmodifiers Escape
done

xdotool windowactivate --sync "$main_window"
xdotool windowfocus --sync "$main_window"
xdotool key --clearmodifiers ctrl+o
dialog_window=$(wait_window 'Select Folder')
xdotool windowactivate --sync "$dialog_window"
xdotool windowfocus --sync "$dialog_window"
xdotool key --clearmodifiers ctrl+l
xdotool type --clearmodifiers --delay 1 "$fixtures"
xdotool key --clearmodifiers Return
for attempt in $(seq 1 100); do
    count=$(find "$XDG_CACHE_HOME" -name '*.thumb' -type f 2>/dev/null | wc -l)
    if [ "$count" -ge 5 ]; then
        break
    fi
    sleep 0.1
done
test "$count" -eq 5
printf 'decoded thumbnails: %s\n' "$count" > "$output/gallery.txt"
grep -o '/[^ ]*/qt6/plugins/[^ ]*\.so' "/proc/$app_pid/maps" | LC_ALL=C sort -u > "$output/loaded-plugins.txt"
for plugin in libqxcb.so libqsvgicon.so libqjpeg.so libqwebp.so libqgif.so; do
    grep -F "/$plugin" "$output/loaded-plugins.txt" > /dev/null
done
xdotool windowactivate --sync "$main_window"
xdotool windowfocus --sync "$main_window"
xdotool key --clearmodifiers alt+F4
timeout 15 tail --pid="$app_pid" -f /dev/null
