#!/bin/bash
# Run from the prepared offline source bundle (or pass its root as the first argument).
set -euo pipefail
root=$(realpath "${1:-.}")
work="$root/work"
test -d "$work/source" && test -d "$work/sysroot"
test -f "$work/mpv-0.37.0/meson.build"
bash "$root/build-mpv.sh" "$work/mpv-0.37.0" "$work/mpv-build" "$work/sysroot"
install -d "$work/private-mpv"
arm-linux-gnueabihf-strip --strip-unneeded "$work/mpv-build/build/libmpv.so.2.2.0" -o "$work/private-mpv/libmpv.so.2"
install -m644 "$work/mpv-0.37.0/LICENSE.GPL" "$work/private-mpv/LICENSE.GPL"
bash "$work/build-wili.sh"
bash "$work/package.sh" "$root/out"
