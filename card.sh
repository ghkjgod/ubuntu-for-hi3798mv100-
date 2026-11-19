#!/bin/sh
# Run on the original eMMC Linux over an authenticated connection.
set -eu
action=${1:?inspect | format | resume | mount-installed | receive | install}
target=/tmp/noble-card
scan() {
    root_num=$(awk '$5=="/" {print $3; exit}' /proc/self/mountinfo)
    root_node=$(sed -n 's/^DEVNAME=//p' "/sys/dev/block/$root_num/uevent")
    test "$root_node" = mmcblk0p9
    test "$(cat /sys/class/block/mmcblk0/device/type)" = MMC
    external=0
    for c in /sys/class/block/mmcblk*; do
        test -r "$c/device/type" && test ! -e "$c/partition" || continue
        test "$(cat "$c/device/type")" = SD || continue
        parent=$c
        external=$((external+1))
    done
    test "$external" = 1
    set -- "$parent"p*
    test "$#" = 1 && test "$(cat "$1/partition")" = 1
    part=$1
    node=/dev/${part##*/}
    test -b "$node"
    test "$(cat "$parent/ro")$(cat "$part/ro")" = 00
    identity="$(cat "$parent/device/cid") $(cat "$parent/device/name") $(cat "$parent/size") $(cat "$part/start") $(cat "$part/size")"
}
scan
printf '%s\n' "$node $identity" >&2
test "$action" != inspect || exit 0
test "$identity" = "${2:?expected CID name sectors start partition-sectors}"
case "$action" in
mount-installed|resume)
    test "$(blkid -s TYPE -o value "$node")" = ext4
    test "$(blkid -s UUID -o value "$node")" = "${3:?expected filesystem UUID}"
    mkdir -p "$target"
    if ! mountpoint -q "$target"; then
        /etc/init.d/S95emuelec stop
        /usr/bin/histb-tf-storage stop
        scan
        test "$identity" = "$2"
        test "$(blkid -s UUID -o value "$node")" = "$3"
        mount -t ext4 -o rw,noatime,dev,suid,exec "$node" "$target"
    fi
    awk -v d="$(cat "$part/dev")" -v m="$target" '$3==d && $4=="/" && $5==m {ok=1} END{exit !ok}' /proc/self/mountinfo
    marker=.noble-installed
    test "$action" != resume || marker=.noble-installing
    test "$(cat "$target/$marker")" = "$identity"
    ;;
format)
    test "${3:-}" = I_AUTHORIZE_FORMAT_EXTERNAL_TF_EXISTING_PARTITION_ONLY
    test ! -e "$target/.noble-installing"
    /etc/init.d/S95emuelec stop
    /usr/bin/histb-tf-storage stop
    test ! -e /var/run/histb-tf-storage.env
    mkdir -p "$target"
    sync
    scan
    test "$identity" = "$2"
    awk -v d="$(cat "$part/dev")" -v m="$target" '$3==d || $5==m {bad=1} END{exit bad}' /proc/self/mountinfo
    /sbin/mke2fs -F -E nodiscard -t ext4 -m 1 -L NOBLE_TF -O none,has_journal,ext_attr,resize_inode,dir_index,filetype,extent,sparse_super,large_file "$node"
    mount -t ext4 -o noatime "$node" "$target"
    echo "$identity" > "$target/.noble-installing"
    sync
    umount "$target"
    mount -t ext4 -o noatime "$node" "$target"
    test "$(cat "$target/.noble-installing")" = "$2"
    blkid "$node"
    ;;
receive|install)
    awk -v d="$(cat "$part/dev")" -v m="$target" '$3==d && $4=="/" && $5==m {ok=1} END{exit !ok}' /proc/self/mountinfo
    test "$(cat "$target/.noble-installing")" = "$identity"
    cd "$target"
    mkdir -p .noble-install
    case "$action" in
    receive)
        case "$3" in tar-helper.tar.gz|ubuntu-noble-armhf-xfce.tar.gz) ;; *) exit 3 ;; esac
        cat > ".noble-install/$3.partial"
        echo "$4  .noble-install/$3.partial" | sha256sum -c - >&2
        mv ".noble-install/$3.partial" ".noble-install/$3"
        ;;
    install)
        h="$target/.noble-install/tools"
        mkdir -p "$h"
        tar -xzf .noble-install/tar-helper.tar.gz -C "$h"
        "$h/ld-linux-armhf.so.3" --library-path "$h" "$h/tar" --version
        "$h/ld-linux-armhf.so.3" --library-path "$h" "$h/tar" --numeric-owner --xattrs --xattrs-include='*' --acls -xzpf .noble-install/ubuntu-noble-armhf-xfce.tar.gz
        test "$(chroot . dpkg --print-architecture)" = armhf
        uuid=$(chroot . /usr/sbin/blkid -s UUID -o value "$node" 2>/dev/null || blkid -s UUID -o value "$node")
        test -n "$uuid"
        printf 'UUID=%s / ext4 defaults,noatime 0 1\n' "$uuid" > etc/fstab
        echo "$uuid" > etc/noble-tf-uuid
        rm -r .noble-install
        mv .noble-installing .noble-installed
        sync
        df -k "$target"
        ;;
    esac
    ;;
*) exit 2 ;;
esac
