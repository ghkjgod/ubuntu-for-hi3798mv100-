#!/bin/sh
# PID 1 on the original p9 filesystem. The kernel and its command line remain in eMMC.
PATH=/sbin:/bin:/usr/sbin:/usr/bin
export PATH
root=/noble-system
state=$root/etc/histb-boot

stock() {
    echo "HISTB_EMMC_STOCK $*" >/dev/console
    cd /
    exec /bin/busybox init
}

[ "$$" = 1 ] || exit 2
[ -x "$root/sbin/init" ] && [ -r "$root/etc/os-release" ] || stock "Ubuntu unavailable"
[ -r /proc/mounts ] || mount -t proc proc /proc || stock "early proc mount failed"
mount -o remount,rw / || stock "p9 remount failed"
if [ -e "$state/stock" ]; then
    stock "operator selected recovery"
elif [ -e "$state/once" ]; then
    mv "$state/once" "$state/consumed" || stock "could not consume trial"
    sync
elif [ ! -e "$state/default" ]; then
    stock "unconfirmed trial"
fi

mount --bind "$root" "$root" || stock "root bind failed"
# The kernel may already have mounted devtmpfs. A fresh mount exposes all built-in devices.
for fs in dev proc sys run; do
    case "$fs" in
        dev) type=devtmpfs ;;
        proc) type=proc ;;
        sys) type=sysfs ;;
        run) type=tmpfs ;;
    esac
    mount -t "$type" "$type" "$root/$fs" || stock "could not mount $fs"
done
# Optional local bench services are consumed once and live only in this boot's /run.
if [ -d /noble-stage/next-run ]; then
    [ ! -L /noble-stage/next-run ] && [ ! -e /noble-stage/used-run ] || stock "incomplete one-time provisioning"
    cp -a /noble-stage/next-run/. "$root/run/" || stock "temporary services failed"
    mv /noble-stage/next-run /noble-stage/used-run || stock "could not consume temporary services"
    rm -r /noble-stage/used-run || stock "could not clear consumed temporary services"
    sync
fi
cd "$root" || stock "root inaccessible"
pivot_root . old-root || stock "pivot failed"
cd /
echo HISTB_EMMC_UBUNTU >/dev/console
/bin/umount -l /old-root
exec /sbin/init
