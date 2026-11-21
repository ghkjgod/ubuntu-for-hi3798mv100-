#!/usr/bin/env bash
# Install on the running 6.12 desktop over ordinary SSH.
set -euo pipefail
here=$(cd "$(dirname "$0")" && pwd)
kernel=6.12.114-hi3798-mv100-exp
[[ $(id -u) == 0 && $(uname -r) == "$kernel" ]]
tr '\0' '\n' </proc/device-tree/compatible | grep -Fxq hisilicon,hi3798mv100
module=${VDH_MODULE:-$here/prebuilt/mv100-vdh.ko}
if [[ $module == "$here/prebuilt/mv100-vdh.ko" ]]; then
    (cd "$here/prebuilt" && sha256sum -c SHA256SUMS)
fi
[[ $(modinfo -F name "$module") == mv100_vdh ]]
[[ $(modinfo -F vermagic "$module") == "$kernel "* ]]
tmp=$(mktemp -d)
trap 'rm -rf -- "$tmp"' EXIT
python3 "$here/firmware.py" "$tmp" "$@"
packages=(ffmpeg gstreamer1.0-tools gstreamer1.0-plugins-base-apps gstreamer1.0-plugins-good gstreamer1.0-plugins-bad gstreamer1.0-libav gstreamer1.0-x)
missing=()
for pkg in "${packages[@]}"; do
    [[ $(dpkg-query -W -f='${db:Status-Status}' "$pkg" 2>/dev/null || :) == installed ]] || missing+=("$pkg")
done
if (( ${#missing[@]} )); then
    apt-get update
    apt-get install -y --no-install-recommends "${missing[@]}"
fi
# Finish a running playback before replacing its module.
if [[ -d /sys/module/mv100_vdh ]]; then rmmod mv100_vdh; fi
atomic_install() {
    local src=$1 dst=$2 mode=$3 staging
    install -d "$(dirname "$dst")"
    staging=$(mktemp "$(dirname "$dst")/.vdh-update.XXXXXXXX")
    install -m "$mode" "$src" "$staging"
    mv -fT "$staging" "$dst"
}
for codec in hevc h264; do
    name=mv100-$codec-cabac.bin
    atomic_install "$tmp/$name" "/lib/firmware/hisilicon/$name" 644
done
atomic_install "$module" "/lib/modules/$kernel/updates/mv100-vdh.ko" 644
atomic_install "$here/histb-play" /usr/local/bin/histb-play 755
atomic_install "$here/histb-play.desktop" /usr/local/share/applications/histb-play.desktop 644
atomic_install "$here/70-mv100-vdh.rules" /etc/udev/rules.d/70-mv100-vdh.rules 644
if command -v update-desktop-database >/dev/null; then
    update-desktop-database /usr/local/share/applications
fi
udevadm control --reload-rules
depmod "$kernel"
modprobe mv100_vdh
udevadm settle
test -r /sys/devices/platform/mv100-vdh/decoded_frames
export GST_REGISTRY="$tmp/gst-registry.bin"
gst-inspect-1.0 --exists v4l2slh264dec
gst-inspect-1.0 --exists v4l2slh265dec
printf '%s\n' mv100_vdh >"$tmp/load.conf"
atomic_install "$tmp/load.conf" /etc/modules-load.d/mv100-vdh.conf 644
command -v histb-play ffplay
