#!/bin/bash
# Convert this project's earlier TF-root trial to a data volume once eMMC is running Ubuntu.
set -euo pipefail
[[ $EUID == 0 && $# == 3 && $3 == FORMAT_DEDICATED_TF_TRIAL_AS_DATA ]] || {
    echo "usage: sudo $0 'CID NAME SECTORS START LENGTH' EXT4_UUID FORMAT_DEDICATED_TF_TRIAL_AS_DATA" >&2
    exit 2
}
expected=$1
uuid=$2
[[ $uuid =~ ^[[:xdigit:]]{8}(-[[:xdigit:]]{4}){3}-[[:xdigit:]]{12}$ ]]
[[ $(findmnt -rn -o MAJ:MIN --target / | tr -d ' ') == "$(cat /sys/class/block/mmcblk0p9/dev)" ]]
[[ $(cat /sys/class/block/mmcblk0/device/type) == MMC ]]
[[ $(findmnt -n -o FSROOT --target /) == /noble-system ]]
part=$(readlink -e "/dev/disk/by-uuid/$uuid")
[[ -b $part ]]
base=/sys/class/block/${part##*/}
parent=$(readlink -f "$base/..")
check_identity() {
    local partitions=("$parent"/*/partition)
    [[ -r ${partitions[0]} && ${#partitions[@]} == 1 && $(cat "$base/partition") == 1 ]]
    [[ $(cat "$parent/device/type") == SD && $(cat "$base/ro") == 0 && $(cat "$parent/ro") == 0 ]]
    [[ $(cat "$parent/device/cid")\ $(cat "$parent/device/name")\ $(cat "$parent/size")\ $(cat "$base/start")\ $(cat "$base/size") == "$expected" ]]
    [[ $(blkid -s UUID -o value "$part") == "$uuid" ]]
}
check_identity
test -e /srv/tf/.noble-installed
[[ $(stat -c %d /srv/tf/.noble-installed) == "$(stat -c %r "$part")" ]]
[[ $(cat /srv/tf/.noble-installed) == "$expected" ]]
# Preserve the trial user's files on eMMC through format, remount and independent archive verification.
umask 077
backup=$(mktemp -d /var/tmp/histb-tf-home.XXXXXXXX)
tar --xattrs --acls -czpf "$backup/home.tar.gz" -C /srv/tf/home desktop
tar -tzf "$backup/home.tar.gz" >/dev/null
hash=$(sha256sum "$backup/home.tar.gz" | cut -d' ' -f1)
echo "Verified trial home backup on eMMC: $backup/home.tar.gz $hash"
systemctl stop srv-tf.automount srv-tf.mount
check_identity
if findmnt -nr -o MAJ:MIN | grep -Fx "$(cat "$base/dev")" >/dev/null; then echo 'SD still mounted' >&2; exit 1; fi
mkfs.ext4 -F -E nodiscard -m 1 -U "$uuid" -L UBUNTU_DATA -O none,has_journal,ext_attr,resize_inode,dir_index,filetype,extent,sparse_super,large_file "$part"
e2fsck -fn "$part"
udevadm trigger --action=change "$base"
udevadm settle
systemctl start srv-tf.automount
systemctl start srv-tf.mount
[[ $(stat -c %d /srv/tf/.) == "$(stat -c %r "$part")" ]]
install -d -m 0755 -o desktop -g desktop /srv/tf/desktop
install -o desktop -g desktop -m 0600 "$backup/home.tar.gz" /srv/tf/desktop/tf-trial-home.tar.gz
echo "$hash  /srv/tf/desktop/tf-trial-home.tar.gz" | sha256sum -c -
sync
findmnt -T /srv/tf/desktop -o TARGET,SOURCE,MAJ:MIN,FSTYPE,OPTIONS
df -h / /srv/tf/desktop
