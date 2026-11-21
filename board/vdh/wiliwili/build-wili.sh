#!/bin/bash
set -eu
export PATH=/usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin
t=$(cd "$(dirname "$0")" && pwd)
export PKG_CONFIG_SYSROOT_DIR="$t/sysroot" PKG_CONFIG_LIBDIR="$t/sysroot/usr/lib/arm-linux-gnueabihf/pkgconfig:$t/sysroot/usr/share/pkgconfig" PKG_CONFIG_PATH=
cmake -G Ninja -S "$t/source" -B "$t/build" -DCMAKE_TOOLCHAIN_FILE="$t/toolchain.cmake" -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=/usr -DPLATFORM_DESKTOP=ON -DUSE_GLES2=ON -DINSTALL=ON -DUSE_LIBROMFS=OFF -DUSE_SYSTEM_CURL=ON -DCPR_USE_SYSTEM_CURL=ON -DUSE_SYSTEM_OPENCC=ON -DGLFW_BUILD_X11=ON -DGLFW_BUILD_WAYLAND=OFF -DANALYTICS=OFF >"$t/../configure.log" 2>&1
cmake --build "$t/build" --target wiliwili -j10 >"$t/../build.log" 2>&1
