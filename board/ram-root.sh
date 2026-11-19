#!/bin/sh
# Temporary PID 1 handover from this board's BusyBox init. Power cycling discards the binding.
set -eu
r=${1:?mount point of the installed Ubuntu TF}
test "$(readlink /proc/1/exe)" = /bin/busybox
test -f "$r/.noble-installed" && test -x "$r/sbin/init"
case "$r" in /tmp/noble-card|/media/emuelec-tf) ;; *) exit 2 ;; esac
part=$(awk -v m="$r" '$4=="/" && $5==m {print $3}' /proc/self/mountinfo)
stored=$(cat "$r/etc/noble-tf-uuid")
actual=$(blkid -s UUID -o value "/dev/$(sed -n 's/^DEVNAME=//p' "/sys/dev/block/$part/uevent")")
test -n "$stored" && test "$stored" = "$actual"
test "$(cat "/sys/dev/block/$part/../device/type")" = SD
test "$(awk '$5=="/"{print $3; exit}' /proc/self/mountinfo)" != "$part"
for fs in dev proc sys; do
    mountpoint -q "$r/$fs" || mount --bind "/$fs" "$r/$fs"
done
mountpoint -q "$r/dev/pts" || mount --bind /dev/pts "$r/dev/pts"
mountpoint -q "$r/run" || mount -t tmpfs tmpfs "$r/run"
mkdir -p "$r/old-root"
test -z "$(ls -A "$r/old-root")"
cat > "$r/run/noble-take-root" <<EOF
#!/bin/sh
set -e
exec >/dev/console 2>&1
echo NOBLE_PIVOT_BEGIN
cd "$r"
/sbin/pivot_root . old-root
cd /
/bin/umount -l /old-root
echo NOBLE_PIVOT_ROOT
/bin/cat /proc/self/mountinfo | /bin/grep ' / / '
exec /sbin/init
EOF
# The temporary table replaces shutdown actions so stock init cannot unmount the new root.
tab=$(mktemp /tmp/noble-inittab.XXXXXX)
printf '::restart:/bin/sh %s/run/noble-take-root\n::shutdown:/bin/sync\n' "$r" > "$tab"
mount --bind "$tab" /etc/inittab
kill -HUP 1
sleep 2
sync
kill -QUIT 1
