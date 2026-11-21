#!/usr/bin/env bash
set -euo pipefail
here=$(cd "$(dirname "$0")" && pwd)
kernel=$(realpath "${1:?usage: build.sh KERNEL_SOURCE KERNEL_OUT MODULE_BUILD_DIR}")
out=$(realpath "${2:?kernel output directory}")
build=$(realpath -m "${3:?module build directory on a Linux filesystem}")
release=6.12.114-hi3798-mv100-exp
test -s "$out/Module.symvers"
grep -Fxq "$release" "$out/include/config/kernel.release"
mkdir -p "$build"
cp "$here"/{Kbuild,vdh.h,vdh-hw.c,vdh-video.c} "$build/"
make -C "$kernel" O="$out" M="$build" ARCH=arm CROSS_COMPILE="${CROSS_COMPILE-arm-linux-gnueabihf-}" modules
modinfo -F vermagic "$build/mv100-vdh.ko" | grep -F "$release "
sha256sum "$build/mv100-vdh.ko"
