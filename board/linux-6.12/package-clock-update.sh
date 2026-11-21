#!/bin/sh
set -eu
here=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
case ${1:-} in
    gpu500) name=gpu-uImage ;;
    cpu1200) name=cpu-uImage ;;
    hdmi-rsen) name=rsen-uImage ;;
    *) echo 'Usage: package-clock-update.sh {gpu500|cpu1200|hdmi-rsen} UIMAGE OUTPUT.tar.gz' >&2; exit 2 ;;
esac
shift
image=$(realpath "${1:?pass the board uImage}")
output=$(realpath -m "${2:?pass the output tar.gz path}")
stage=$(mktemp -d)
trap 'rm -rf -- "$stage"' EXIT
install -m644 "$image" "$stage/$name"
install -m644 "$here/display-slot.py" "$stage/display-slot.py"
install -m755 "$here/rootfs/usr/local/bin/histb-performance" "$stage/histb-performance"
cd "$stage"
sha256sum "$name" display-slot.py histb-performance > SHA256SUMS
tar --sort=name --owner=0 --group=0 --numeric-owner --mtime=@1793059200 -czf "$output" SHA256SUMS display-slot.py "$name" histb-performance
