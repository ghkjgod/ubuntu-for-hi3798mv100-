#!/usr/bin/env bash
set -euo pipefail
here=$(cd "$(dirname "$0")" && pwd)
work=$(realpath -m "${1:?new Mesa build directory}")
root=$(realpath -m "${2:?target rootfs staging directory}")
version=25.2.8-0ubuntu0.24.04.5
library="libgallium-$version.so"
mirror=${UBUNTU_PORTS_MIRROR:-https://mirrors.tuna.tsinghua.edu.cn/ubuntu-ports}
meson=${MESON:-meson}
mkdir -p "$work"
cd "$work"
while read -r hash file; do
    if test -f "${MESA_SOURCE_CACHE:-/nonexistent}/$file"; then
        cp "$MESA_SOURCE_CACHE/$file" "$file"
    else
        curl --fail --location --retry 3 "$mirror/pool/main/m/mesa/$file" -o "$file"
    fi
    printf '%s  %s\n' "$hash" "$file" | sha256sum -c -
done <"$here/SHA256SUMS"
dpkg-source -x "mesa_$version.dsc" source
patch -s -d source -p1 <"$here/large-render-target.patch"
# Ubuntu versions its Gallium symbol namespace with the full package version.
printf '%s\n' "$version" >source/VERSION
sysroot=${MESA_SYSROOT:-$work/sysroot}
if test -z "${MESA_SYSROOT:-}"; then
    mkdir -p sysroot apt/lists/partial apt/archives/partial apt/log
    touch apt/status
    for suite in noble noble-updates noble-security; do
        printf 'deb [arch=armhf signed-by=/usr/share/keyrings/ubuntu-archive-keyring.gpg] %s %s main universe\n' "$mirror" "$suite"
    done >apt/sources.list
    cat >apt/config <<EOF
APT::Architecture "armhf";
APT::Architectures { "armhf"; };
Dir::Etc::sourcelist "$work/apt/sources.list";
Dir::Etc::sourceparts "-";
Dir::State "$work/apt";
Dir::State::status "$work/apt/status";
Dir::State::lists "$work/apt/lists";
Dir::Cache "$work/apt";
Dir::Log "$work/apt/log";
Debug::NoLocking "true";
Acquire::Languages "none";
EOF
    APT_CONFIG="$work/apt/config" apt-get update
    APT_CONFIG="$work/apt/config" apt-get --no-install-recommends --download-only -y install \
        libdrm-dev libexpat1-dev zlib1g-dev libzstd-dev libelf-dev libx11-xcb-dev \
        libxcb-dri3-dev libxcb-present-dev libxcb-sync-dev libxcb-xfixes0-dev libxshmfence-dev \
        libxrandr-dev libxxf86vm-dev libxcb-glx0-dev libxext-dev libxcb-randr0-dev \
        libglvnd-dev libxcb-shm0-dev
    for pkg in apt/archives/*.deb; do dpkg-deb -x "$pkg" "$sysroot"; done
    test -e "$sysroot/lib" || ln -s usr/lib "$sysroot/lib"
fi
sysroot=$(realpath "$sysroot")
test -r "$sysroot/usr/lib/arm-linux-gnueabihf/pkgconfig/libdrm.pc"
cat >cross.ini <<EOF
[binaries]
c = 'arm-linux-gnueabihf-gcc'
cpp = 'arm-linux-gnueabihf-g++'
ar = 'arm-linux-gnueabihf-ar'
strip = 'arm-linux-gnueabihf-strip'
pkg-config = 'pkg-config'
[properties]
sys_root = '$sysroot'
pkg_config_libdir = '$sysroot/usr/lib/arm-linux-gnueabihf/pkgconfig:$sysroot/usr/share/pkgconfig'
[built-in options]
c_args = ['-ffile-prefix-map=$work=.', '-I$sysroot/usr/include/arm-linux-gnueabihf', '-I$sysroot/usr/include']
cpp_args = ['-ffile-prefix-map=$work=.', '-I$sysroot/usr/include/arm-linux-gnueabihf', '-I$sysroot/usr/include']
c_link_args = ['--sysroot=$sysroot', '-L$sysroot/usr/lib/arm-linux-gnueabihf', '-Wl,-rpath-link,$sysroot/usr/lib/arm-linux-gnueabihf']
cpp_link_args = ['--sysroot=$sysroot', '-L$sysroot/usr/lib/arm-linux-gnueabihf', '-Wl,-rpath-link,$sysroot/usr/lib/arm-linux-gnueabihf']
[host_machine]
system = 'linux'
cpu_family = 'arm'
cpu = 'cortex-a7'
endian = 'little'
EOF
export SOURCE_DATE_EPOCH=1793059200
setup_args=(setup out source --cross-file cross.ini --buildtype=release \
    -Dplatforms=x11 -Dgallium-drivers=lima -Dvulkan-drivers= -Dllvm=disabled \
    -Dglx=dri -Dgbm=enabled -Degl=enabled -Dglvnd=enabled -Dgallium-va=disabled \
    -Dgallium-vdpau=disabled -Dvideo-codecs= -Dbuild-tests=false -Dvalgrind=disabled \
    -Dlmsensors=disabled -Dlibunwind=disabled)
for attempt in 1 2 3; do
    if "$meson" "${setup_args[@]}" >setup.log 2>&1; then
        break
    fi
    cat setup.log >&2
    if test "$attempt" = 3 || ! grep -q 'Clock skew detected' setup.log; then
        exit 1
    fi
    sleep 4
done
ninja -C out -j "${JOBS:-$(nproc)}" "src/gallium/targets/dri/$library"
# Meson adds the cross sysroot to the uninstalled DSO's runtime search path.
# Relink the target without that build-only path before stripping the board copy.
python3 - "$sysroot" "src/gallium/targets/dri/$library" <<'RELINK'
import shlex
import subprocess
import sys
path, target = sys.argv[1:]
recipe = subprocess.check_output(["ninja", "-t", "commands", "-s", target], cwd="out", text=True)
args = shlex.split(recipe)
rpath = "-Wl,-rpath," + path + "/usr/lib/arm-linux-gnueabihf"
if rpath in args:
    args.remove(rpath)
    subprocess.run(args, cwd="out", check=True)
RELINK
dest="$root/usr/local/lib/histb-lima/mesa"
docs="$root/usr/share/doc/histb-lima/mesa"
mkdir -p "$dest" "$docs"
arm-linux-gnueabihf-strip -o "$dest/$library" "out/src/gallium/targets/dri/$library"
install -m644 source/debian/copyright source/docs/license.rst "$docs/"
cp -r source/licenses "$docs/"
