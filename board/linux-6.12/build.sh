#!/usr/bin/env bash
set -euo pipefail
here=$(cd "$(dirname "$0")" && pwd)
work=$(realpath -m "${1:?provide a new Linux build directory}")
: "${MV100_SDK_ROOT:?set the path to the prepared HiSTBLinux SDK}"
: "${MV100_SDK_BOOT:?set the SDK board boot object directory}"
test -s "$MV100_SDK_BOOT/product/driver/hdmi/libhdmi.a"
test -s "$MV100_SDK_BOOT/product/driver/disp/libdisp.a"
commit=6bf33e9dead46a7a2dd0dac34e1c0c3b50c46ce7
fw_commit=73e8acec66b0a91513e0699567b7854f4161d8b5
fw_url="https://kernel.googlesource.com/pub/scm/linux/kernel/git/firmware/linux-firmware/+/$fw_commit"
mkdir -p "$work"
cd "$work"
mkdir source out package
archive=${SOURCE_ARCHIVE:-$work/linux.tar.gz}
if ! test -f "$archive"; then
    curl -fL --connect-timeout 15 --retry 3 "https://codeload.github.com/duskbreakmist/hi3798-linux/tar.gz/$commit" -o "$archive"
fi
check() { printf '%s  %s\n' "$1" "$2" | sha256sum -c -; }
check 92902f383c90d16f8b8ef472de69f2ee7b0c462ff754828f66183d7bb3da3109 "$archive"
tar -xf "$archive" -C source --strip-components=1
patch -s -d source -p1 <"$here/board.patch"
patch -s -d source -p1 <"$here/display-modes.patch"
patch -s -d source -p1 <"$here/irq-timing.patch"
patch -s -d source -p1 <"$here/pcm-drain.patch"
if [[ ${MV100_HDMI_RSEN:-0} == 1 ]]; then
    patch -s -d source -p1 <"$here/rsen-live-edid.patch"
fi
if [[ ${MV100_GPU500:-0} == 1 || ${MV100_CPU1200:-0} == 1 ]]; then
    patch -s -d source -p1 <"$here/gpu500.patch"
fi
if [[ ${MV100_CPU1200:-0} == 1 ]]; then
    patch -s -d source -p1 <"$here/cpu1200.patch"
fi
if test -f "${INPUT_CACHE:-/nonexistent}/upstream-v6.12.114-crg.c"; then
    cp "$INPUT_CACHE/upstream-v6.12.114-crg.c" .
else
    curl -fLsS --connect-timeout 15 --retry 3 https://raw.githubusercontent.com/gregkh/linux/v6.12.114/drivers/clk/hisilicon/crg-hi3798cv200.c -o upstream-v6.12.114-crg.c
fi
check c9ccf75f59fd0bcbb2d690dc9986c4c0d95a24170e3b47a358b79f198315c86f upstream-v6.12.114-crg.c
export ARCH=arm CROSS_COMPILE=${CROSS_COMPILE:-arm-linux-gnueabihf-} OUT_DIR="$work/out"
# Separate the freestanding kernel configuration from the armhf libc used by
# the two userspace tools. Otherwise installing libc changes embedded IKCONFIG.
export USERCFLAGS=-nostdinc
cp "$here/ec6108.config" out/.config
if [[ ${MV100_CPU1200:-0} == 1 ]]; then
    source/scripts/config --file out/.config --enable CPU_FREQ_THERMAL --enable THERMAL_GOV_BANG_BANG --disable THERMAL_DEFAULT_GOV_STEP_WISE --enable THERMAL_DEFAULT_GOV_BANG_BANG
fi
make -s -C source O="$OUT_DIR" olddefconfig
bash source/hi3798/build-mv100.sh build
cp out/uImage-mv100-codex-lima-mp2 package/uImage
cp -r "$here/rootfs" package/
root="$work/package/rootfs"
mkdir -p "$root/usr/lib/firmware/rtlwifi" "$root/usr/share/doc/histb-lima"
for path in rtlwifi/rtl8188fufw.bin LICENSES/LICENCE.rtlwifi_firmware.txt; do
    if test -f "${INPUT_CACHE:-/nonexistent}/${path##*/}"; then
        cp "$INPUT_CACHE/${path##*/}" .
    else
        curl -fLsS --connect-timeout 15 --retry 3 "$fw_url/$path?format=TEXT" | base64 -d > "${path##*/}"
    fi
done
check e6189d6d7032b83f0ed4284629c43db66262449a866f33fd329e611239f62e1f rtl8188fufw.bin
check a61351665b4f264f6c631364f85b907d8f8f41f8b369533ef4021765f9f3b62e LICENCE.rtlwifi_firmware.txt
install -m644 rtl8188fufw.bin "$root/usr/lib/firmware/rtlwifi/"
install -m644 LICENCE.rtlwifi_firmware.txt "$root/usr/share/doc/histb-lima/"
sh source/hi3798/display-init/rootfs/build-kmsro-select.sh "$root/usr/local/lib/histb-lima"
"${CROSS_COMPILE}gcc" -O2 source/hi3798/display-init/lima-render-image.c -ldl -o "$root/usr/local/bin/histb-lima-test"
install -m755 "$here/../../overlay/usr/local/bin/start-histb-session" "$here/../../overlay/usr/local/bin/histb-chromium" "$root/usr/local/bin/"
chmod 755 "$root"/usr/local/bin/histb-*
release=$(cat out/include/config/kernel.release)
mkdir -p "$root/usr/lib/modules/$release"
install -m644 out/modules.{builtin,builtin.modinfo,order} "$root/usr/lib/modules/$release/"
install -m755 "$here/check-desktop.sh" "$here/activate.sh" package/
chmod 755 "$root/usr/local/sbin/histb-zram" "$root/usr/local/sbin/histb-wifi-stop"
chmod 644 "$root"/etc/systemd/system/histb-*.service "$root"/etc/xdg/autostart/histb-*.desktop
wants="$root/etc/systemd/system/multi-user.target.wants"
mkdir -p "$wants"
for unit in histb-performance histb-zram histb-wifi-shutdown; do
    ln -s "../$unit.service" "$wants/$unit.service"
done
sh "$here/chromium/prepare.sh" "$root/usr/local/share/histb-lima/chromium-egl"
install -m755 "$here/chromium/install.sh" "$root/usr/local/sbin/histb-chromium-egl"
bash "$here/mesa/build.sh" "$work/mesa" "$root"
bash "$here/chromium/mesa/build.sh" "$work/browser-mesa" "$root"
sh "$here/package.sh" "$work/package"
