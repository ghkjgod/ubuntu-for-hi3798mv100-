#!/bin/bash
set -euo pipefail
root=$(realpath "$1")
output=$(realpath "$2")
tmp=$(mktemp -d "$(dirname "$root")/tar-helper.XXXXXXXX")
cp -L "$root/lib/ld-linux-armhf.so.3" "$root/usr/bin/tar" "$tmp/"
chroot "$root" ldd /usr/bin/tar | awk '/=> \// {print $3}' | while read -r lib; do
    cp -L "$root$lib" "$tmp/"
done
tar -C "$tmp" -czf "$output/tar-helper.tar.gz" .
