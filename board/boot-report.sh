#!/bin/sh
set -u
exec > /var/log/noble-board-boot.log 2>&1
cat /etc/os-release
uname -a
cat /proc/cmdline
cat /proc/1/comm
findmnt -no SOURCE,MAJ:MIN,FSTYPE,OPTIONS /
blkid
dpkg --print-architecture
df -h /
ls /dev/input
cat /proc/bus/input/devices
cat /proc/msp/disp1
cat /proc/msp/hdmi0
systemctl --no-pager --failed
ps -eo user,comm,args | grep -E 'Xorg|xfce4-session|xfdesktop|xfwm4|lightdm'
sync
cat /var/log/noble-board-boot.log >/dev/console
