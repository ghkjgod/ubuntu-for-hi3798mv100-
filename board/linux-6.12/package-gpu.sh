#!/bin/sh
set -eu
here=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
image=$(realpath "${1:?pass the GPU-enabled uImage}")
output=$(realpath -m "${2:?pass the output tar.gz path}")
stage=$(mktemp -d)
trap 'rm -rf -- "$stage"' EXIT
install -m644 "$image" "$stage/gpu-uImage"
install -m644 "$here/display-slot.py" "$stage/display-slot.py"
install -m755 "$here/rootfs/usr/local/bin/histb-performance" "$stage/histb-performance"
cd "$stage"
sha256sum gpu-uImage display-slot.py histb-performance > SHA256SUMS
tar --sort=name --owner=0 --group=0 --numeric-owner --mtime=@1793059200 -czf "$output" SHA256SUMS display-slot.py gpu-uImage histb-performance
