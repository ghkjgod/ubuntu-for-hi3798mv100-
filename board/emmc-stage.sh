#!/bin/sh
# Invoked over authenticated SSH on the stock p9 system. Only regular files on p9 are changed.
set -eu
action=${1:?inspect | prepare | receive | extract | trial | rollback}
stage=/noble-stage
root=/noble-system
original=../bin/busybox
loader=../noble-stage/emmc-init.sh
identity() {
    dev=$(awk '$4=="/" && $5=="/"{print $3; exit}' /proc/self/mountinfo)
    test "$(cat /sys/class/block/mmcblk0p9/dev)" = "$dev"
    test "$(cat /sys/class/block/mmcblk0/device/type)" = MMC
    test "$(cat /sys/class/block/mmcblk0p9/partition)" = 9
    echo "$(cat /sys/class/block/mmcblk0/device/cid) $(cat /sys/class/block/mmcblk0p9/start) $(cat /sys/class/block/mmcblk0p9/size)"
}
id=$(identity)
test "$(readlink /proc/1/exe)" = /bin/busybox
if [ "$action" = inspect ]; then echo "$id"; df -k /; readlink /sbin/init; exit; fi
test "$id" = "${2:?expected eMMC CID start sectors}"
case "$action" in
prepare)
    test "$(readlink /sbin/init)" = "$original"
    test ! -e "$root"
    if [ -e "$stage" ]; then test "$(cat "$stage/identity")" = "$id"; else
        umask 077
        mkdir "$stage"
        printf '%s\n' "$id" >"$stage/identity"
    fi
    required=${3:-3000000}
    case "$required" in ''|*[!0-9]*) exit 2 ;; esac
    test "$(df -Pk / | awk 'END {print $4}')" -gt "$required"
    ;;
receive)
    test "$(cat "$stage/identity")" = "$id"
    case "$3" in tar-helper.tar.gz|ubuntu-noble-armhf-xfce.tar.gz|emmc-init.sh) ;; *) exit 2 ;; esac
    cd "$stage"
    umask 077
    cat >"$3.partial"
    echo "$4  $3.partial" | sha256sum -c -
    if [ "$3" = emmc-init.sh ]; then chmod 755 "$3.partial"; fi
    mv "$3.partial" "$3"
    ;;
extract)
    test "$(cat "$stage/identity")" = "$id"
    test ! -e "$root"
    test "$(readlink /sbin/init)" = "$original"
    cd "$stage"
    mkdir -p tools unpack
    tar -xzf tar-helper.tar.gz -C tools
    h="$stage/tools"
    "$h/ld-linux-armhf.so.3" --library-path "$h" "$h/tar" --numeric-owner --xattrs --xattrs-include='*' --acls -xzpf ubuntu-noble-armhf-xfce.tar.gz -C unpack
    test "$(chroot unpack dpkg --print-architecture)" = armhf
    chroot unpack /usr/bin/test -x /sbin/init
    mkdir -p unpack/old-root unpack/srv/tf unpack/etc/histb-boot
    chmod 755 emmc-init.sh
    printf '/dev/mmcblk0p9 / ext4 defaults,noatime 0 0\n' >unpack/etc/fstab
    if [ "${3:--}" != - ]; then
        # The external UUID is optional. A missing TF does not delay the root or the desktop.
        uuid=$3
        echo "$uuid" | grep -Eq '^[0-9a-fA-F]{8}-[0-9a-fA-F]{4}-[0-9a-fA-F]{4}-[0-9a-fA-F]{4}-[0-9a-fA-F]{12}$'
        printf 'UUID=%s /srv/tf ext4 nofail,noatime,nodev,nosuid,x-systemd.automount,x-systemd.device-timeout=20s 0 2\n' "$uuid" >>unpack/etc/fstab
        ln -snf /srv/tf/desktop unpack/home/desktop/TF
        chown -h 1000:1000 unpack/home/desktop/TF
    fi
    sync
    mv unpack "$root"
    ;;
trial)
    test "$(cat "$stage/identity")" = "$id" && test -x "$stage/emmc-init.sh"
    current=$(readlink /sbin/init)
    test "$current" = "$original" || test "$current" = "$loader"
    test -x "$root/sbin/init" && test -d "$root/old-root"
    test ! -e "$root/etc/histb-boot/default" && test ! -e "$root/etc/histb-boot/stock"
    mkdir -p "$root/etc/histb-boot"
    touch "$root/etc/histb-boot/once"
    if [ "$current" = "$loader" ]; then sync; exit 0; fi
    test ! -e /sbin/.histb-init-next
    ln -s "$loader" /sbin/.histb-init-next
    sync
    mv -f /sbin/.histb-init-next /sbin/init
    sync
    test "$(readlink /sbin/init)" = "$loader"
    ;;
rollback)
    test "$(cat "$stage/identity")" = "$id"
    test "$(readlink /sbin/init)" = "$loader"
    test ! -e /sbin/.histb-init-next
    ln -s "$original" /sbin/.histb-init-next
    mv -f /sbin/.histb-init-next /sbin/init
    sync
    test "$(readlink /sbin/init)" = "$original"
    ;;
*) exit 2 ;;
esac
