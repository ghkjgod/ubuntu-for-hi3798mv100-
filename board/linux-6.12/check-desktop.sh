#!/bin/sh
set -eu
export DISPLAY=${DISPLAY:-:0}
export XAUTHORITY=${XAUTHORITY:-/root/.Xauthority}
uname -r
findmnt -n -o SOURCE,FSTYPE /
systemctl is-active ssh lightdm NetworkManager
pgrep -x xfce4-session >/dev/null
grep 'glamor X acceleration enabled on Mali450' /var/log/Xorg.0.log
xrandr --current
if command -v histb-es2-info >/dev/null; then
    info=histb-es2-info
else
    info=/usr/bin/es2_info.arm-linux-gnueabihf
fi
"$info" | grep -E '^GL_(VERSION|RENDERER):'
histb-lima-run "$info" | grep '^GL_RENDERER: Mali450'
MESA_LOADER_DRIVER_OVERRIDE=lima histb-lima-test
cat /sys/devices/system/cpu/online
cat /sys/devices/system/cpu/cpufreq/policy0/scaling_available_frequencies
cat /sys/class/devfreq/*gpu/available_frequencies
cat /sys/class/devfreq/*gpu/trans_stat
cat /sys/class/net/eth0/speed /sys/class/net/eth0/duplex
ls /srv/tf >/dev/null
findmnt -n -t ext4 /srv/tf
cat /sys/block/mmcblk1/device/name
grep '^N:' /proc/bus/input/devices
nmcli -t -f DEVICE,TYPE,STATE dev
