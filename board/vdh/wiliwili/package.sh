#!/bin/sh
set -eu
t=$(cd "$(dirname "$0")" && pwd)
out=${1:?supply output directory}
stage=$(mktemp -d "$t/deb-stage.XXXXXXXX")
trap 'rm -rf -- "$stage"' EXIT
mkdir -p "$out" "$stage"
DESTDIR="$stage" cmake -DCMAKE_INSTALL_LOCAL_ONLY=1 -DCMAKE_INSTALL_DO_STRIP=1 -P "$t/build/cmake_install.cmake" >"$t/../stage.log"
mkdir -p "$stage/usr/libexec/wiliwili" "$stage/DEBIAN" "$stage/usr/share/doc/wiliwili"
mv "$stage/usr/bin/wiliwili" "$stage/usr/libexec/wiliwili/wiliwili"
install -m644 "$t/private-mpv/libmpv.so.2" "$stage/usr/libexec/wiliwili/libmpv.so.2"
install -m755 "$t/../wiliwili-launch" "$stage/usr/bin/wiliwili"
install -m644 "$t/source/LICENSE" "$stage/usr/share/doc/wiliwili/copyright"
printf '\nPrivate libmpv 0.37.0, distributed under GPL v2 or later.\n' >>"$stage/usr/share/doc/wiliwili/copyright"
cat "$t/private-mpv/LICENSE.GPL" >>"$stage/usr/share/doc/wiliwili/copyright"
cat >"$stage/DEBIAN/control" <<EOF
Package: wiliwili
Version: 1.6.0-1+mv100.7
Architecture: armhf
Maintainer: Hi3798MV100 local build <build@localhost>
Section: video
Priority: optional
Depends: libc6 (>= 2.38), libstdc++6, libgcc-s1, libmpv2 (>= 0.37), libcurl4t64, libssl3t64, libdbus-1-3, libopencc1.1, libwebp7, zlib1g, libegl1, libgles2, libx11-6, libxrandr2, libxcursor1, libxi6, libxinerama1, xauth, util-linux
Homepage: https://github.com/xfangfang/wiliwili
Description: wiliwili Bilibili client for the EC6108V9C
 ARMv7 hard-float, Cortex-A7 and OpenGL ES 2.0 build for Ubuntu Noble.
 Includes the bundled GLFW frontend, libmpv playback and Xfce launcher.
EOF
readelf -hA "$stage/usr/libexec/wiliwili/wiliwili" | grep -E 'Class:|Machine:|Flags:|Tag_CPU_arch:|Tag_ABI_VFP_args:'
readelf -d "$stage/usr/libexec/wiliwili/wiliwili" |grep -E 'NEEDED|RPATH|RUNPATH'
test -f "$stage/usr/share/wiliwili/resources/font/switch_font.ttf"
dpkg-deb -Zxz -z4 --root-owner-group --build "$stage" "$out/wiliwili_1.6.0-1+mv100.7_armhf.deb"
sha256sum "$out/wiliwili_1.6.0-1+mv100.7_armhf.deb"
