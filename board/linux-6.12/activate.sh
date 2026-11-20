#!/bin/sh
set -eu
test "$(id -u)" = 0
tr '\000' '\n' </proc/device-tree/compatible | grep -qx hisilicon,hi3798mv100
systemctl daemon-reload
systemctl enable --now histb-performance.service histb-zram.service histb-wifi-shutdown.service
if systemctl is-active --quiet NetworkManager &&
   [ -f /etc/NetworkManager/system-connections/histb-ethernet.nmconnection ] &&
   [ "$(nmcli -g GENERAL.NM-MANAGED device show eth0)" = yes ] &&
   systemctl is-active --quiet systemd-networkd &&
   links=$(networkctl --no-legend --no-pager list) && [ -n "$links" ] &&
   ! printf '%s\n' "$links" | awk 'NF && $NF != "unmanaged" { used=1 } END { exit !used }'; then
    systemctl disable --now systemd-networkd-wait-online.service systemd-networkd.socket systemd-networkd.service
fi

/usr/local/sbin/histb-chromium-egl
