#!/bin/bash
set -eu
src=$(realpath "$1")
out=$(realpath -m "$2")
root=$(realpath "$3")
mkdir -p "$out"
cat >"$out/arm.ini" <<EOF
[binaries]
c = 'arm-linux-gnueabihf-gcc'
cpp = 'arm-linux-gnueabihf-g++'
ar = 'arm-linux-gnueabihf-ar'
strip = 'arm-linux-gnueabihf-strip'
pkg-config = '/usr/bin/pkg-config'
[properties]
sys_root = '$root'
pkg_config_libdir = ['$root/usr/lib/arm-linux-gnueabihf/pkgconfig', '$root/usr/share/pkgconfig']
needs_exe_wrapper = true
[built-in options]
c_args = ['--sysroot=$root', '-D_FILE_OFFSET_BITS=64', '-D_TIME_BITS=64', '-mcpu=cortex-a7', '-mfpu=neon-vfpv4']
c_link_args = ['--sysroot=$root', '-Wl,-rpath-link,$root/usr/lib/arm-linux-gnueabihf']
[host_machine]
system = 'linux'
cpu_family = 'arm'
cpu = 'cortex-a7'
endian = 'little'
EOF
meson setup --buildtype=release --cross-file "$out/arm.ini" -Dauto_features=disabled -Dlibmpv=true -Dcplayer=false -Dlua=lua5.2 -Dalsa=enabled -Dgl=enabled -Dplain-gl=enabled -Diconv=enabled "$out/build" "$src"
ninja -C "$out/build" -j10
