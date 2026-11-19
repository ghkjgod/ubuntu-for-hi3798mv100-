#!/bin/bash
set -eu
sdk=$(realpath "${1:?path to built HiSTB SDK}")
out=$(realpath -m "${2:?output directory}")
src=$(dirname "$(realpath "$0")")
inc="$sdk/out/hi3798mv100/hi3798mdmo1g/include"
cc="$sdk/tools/linux/toolchains/arm-histbv310-linux/bin/arm-histbv310-linux-gcc"
mkdir -p "$out"
"$cc" -std=gnu99 -O2 -Wall -Wextra -static -s -I "$inc" "$src/hifb.c" -o "$out/hifb-plane"
"$cc" -std=gnu99 -O2 -Wall -Wextra -static -s -I "$inc" -I "$sdk/source/msp/drv/include" -I "$sdk/source/common/include" -I "$sdk/source/common/drv/include" "$src/sink-mode.c" -o "$out/sink-mode"
