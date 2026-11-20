#!/bin/sh
set -eu
cd "${1:?package staging directory required}"
find rootfs -type d -exec chmod 755 {} +
find rootfs -type f -name '*.conf' -exec chmod 644 {} +
find rootfs -type f -print0 | LC_ALL=C sort -z | xargs -0 sha256sum > CONTENTS.sha256
sha256sum uImage check-desktop.sh activate.sh >> CONTENTS.sha256
tar --sort=name --owner=0 --group=0 --numeric-owner --mtime=@1793059200 -czf ../hi3798mv100-linux-6.12.114-hdmi.tar.gz .
sha256sum ../hi3798mv100-linux-6.12.114-hdmi.tar.gz uImage
