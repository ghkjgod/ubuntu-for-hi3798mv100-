#!/usr/bin/env bash
set -euo pipefail
kernel=6.12.114-hi3798-mv100-exp
[[ $(id -u) == 0 && $(uname -r) == "$kernel" ]]
if [[ -d /sys/module/mv100_vdh ]]; then rmmod mv100_vdh; fi
rm -f /etc/modules-load.d/mv100-vdh.conf /etc/udev/rules.d/70-mv100-vdh.rules \
    /lib/firmware/hisilicon/mv100-{h264,hevc}-cabac.bin \
    "/lib/modules/$kernel/updates/mv100-vdh.ko" /usr/local/bin/histb-play \
    /usr/local/share/applications/histb-play.desktop
if command -v update-desktop-database >/dev/null; then
    update-desktop-database /usr/local/share/applications
fi
udevadm control --reload-rules
depmod "$kernel"
