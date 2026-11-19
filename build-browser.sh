#!/bin/bash
# Prepare a separate Debian armhf Chromium runtime. Ubuntu continues to use Noble packages.
set -euo pipefail
[[ $EUID == 0 && $# == 1 ]] || { echo "usage: sudo $0 NEW_EXT4_DIRECTORY" >&2; exit 2; }
if [[ ${HISTB_BROWSER_MOUNTS:-} != private ]]; then
    exec unshare --mount --propagation private env HISTB_BROWSER_MOUNTS=private bash "$0" "$@"
fi
b=$(realpath -m "$1")
[[ ! -e $b ]] || { echo "Browser build directory already exists" >&2; exit 1; }
mirror=https://mirrors.tuna.tsinghua.edu.cn
debootstrap --arch=armhf --foreign --variant=minbase --keyring=/usr/share/keyrings/debian-archive-keyring.gpg bookworm "$b" "$mirror/debian"
mount -t proc proc "$b/proc"
mount --rbind /dev "$b/dev"
mount -t tmpfs tmpfs "$b/run"
chroot "$b" /debootstrap/debootstrap --second-stage
cp -L /etc/resolv.conf "$b/etc/resolv.conf"
install -Dm644 /etc/ssl/certs/ca-certificates.crt "$b/etc/ssl/certs/ca-certificates.crt"
printf 'deb [signed-by=/usr/share/keyrings/debian-archive-keyring.gpg] %s/debian bookworm main\ndeb [signed-by=/usr/share/keyrings/debian-archive-keyring.gpg] %s/debian-security bookworm-security main\n' "$mirror" "$mirror" >"$b/etc/apt/sources.list"
printf '#!/bin/sh\nexit 101\n' > "$b/usr/sbin/policy-rc.d"
chmod 755 "$b/usr/sbin/policy-rc.d"
export DEBIAN_FRONTEND=noninteractive
chroot "$b" apt-get -o APT::Update::Error-Mode=any update
chroot "$b" apt-get install -y --no-install-recommends chromium chromium-sandbox ca-certificates fonts-dejavu-core
chroot "$b" apt-get clean
chroot "$b" dpkg --audit
chroot "$b" dpkg-query -W -f='${binary:Package}\t${Version}\t${Architecture}\n' >"$b/etc/chromium-runtime-packages.tsv"
chroot "$b" useradd -m -u 1000 -s /bin/sh desktop
mkdir -p "$b/run" "$b/tmp/.X11-unix" "$b/usr/share/fonts/host" "$b/media/tf"
rm -f "$b/usr/sbin/policy-rc.d"
find "$b/var/lib/apt/lists" -type f -delete
: > "$b/etc/machine-id"
: > "$b/etc/resolv.conf"
ln -sfn /etc/machine-id "$b/var/lib/dbus/machine-id"
umount -R "$b/dev"
umount "$b/run"
if mountpoint -q "$b/proc"; then umount "$b/proc"; fi
