#!/bin/bash
# For the combined offline update archive on the existing MV100 Ubuntu desktop.
set -euo pipefail
cd "$(dirname "$0")"
[[ $(id -u) == 0 && $(uname -r) == 6.12.114-hi3798-mv100-exp ]]
tr '\0' '\n' </proc/device-tree/compatible | grep -Fxq hisilicon,hi3798mv100
sha256sum --quiet -c SHA256SUMS
if [[ ${1:-} == --rollback ]]; then
    if [[ -d /sys/module/mv100_vdh ]]; then rmmod mv100_vdh; fi
    target="/lib/modules/$(uname -r)/updates/mv100-vdh.ko"
    install -m644 rollback/mv100-vdh.ko "$target.rollback"
    mv -fT "$target.rollback" "$target"
    install -m755 rollback/histb-play /usr/local/bin/histb-play
    depmod "$(uname -r)"
    modprobe mv100_vdh
    dpkg -i ./rollback/wiliwili_*_armhf.deb
    exit 0
fi
[[ $# == 0 ]]
apt-get -s install ./wiliwili_*_armhf.deb >/dev/null
bash board/vdh/install.sh --from-dir /lib/firmware/hisilicon
test -r /sys/devices/platform/mv100-vdh/avc_refusals
dpkg -i ./wiliwili_*_armhf.deb
dpkg -V wiliwili
