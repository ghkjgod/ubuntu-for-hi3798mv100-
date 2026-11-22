#!/bin/bash
set -Eeuo pipefail
# Ubuntu 24.04 host, native armhf or qemu-arm with a fix-binary binfmt registration.
if (( EUID != 0 || $# != 2 )); then echo "usage: sudo $0 EXT4_WORK_DIRECTORY OUTPUT_DIRECTORY" >&2; exit 2; fi
if [[ ${HISTB_PRIVATE_MOUNTS:-} != yes ]]; then
    exec unshare --mount --propagation private env HISTB_PRIVATE_MOUNTS=yes bash "$0" "$@"
fi
src=$(cd "$(dirname "$0")" && pwd)
: "${HISTB_SDK:?set HISTB_SDK to the built HiSTB SDK directory}"
mkdir -p "$1" "$2"
work=$(realpath "$1")
out=$(realpath "$2")
cache="$work/cache"
mkdir -p "$cache"
root=$(mktemp -d "$work/root.XXXXXXXX")
echo "Building in $root"
name=ubuntu-base-24.04.5-base-armhf.tar.gz
origin=https://cdimage.ubuntu.com/ubuntu-base/releases/24.04/release
keyring=/usr/share/keyrings/ubuntu-archive-keyring.gpg
gpgv --keyring "$keyring" "$src/base/SHA256SUMS.gpg" "$src/base/SHA256SUMS"
if [[ ! -s "$cache/$name" ]]; then
    curl -fL --retry 3 -o "$cache/$name.part" "$origin/$name"
    mv "$cache/$name.part" "$cache/$name"
fi
(cd "$cache"; grep -F " *$name" "$src/base/SHA256SUMS" | sha256sum -c -)
tar --numeric-owner -xpf "$cache/$name" -C "$root"
mkdir -p "$root"/{proc,sys,dev,run}
mount -t proc proc "$root/proc"
mount -t tmpfs tmpfs "$root/run"
mount -t tmpfs tmpfs "$root/dev"
for device in null zero random urandom full tty; do
    touch "$root/dev/$device"
    mount --bind "/dev/$device" "$root/dev/$device"
done
mkdir "$root/dev/pts"
mount -t devpts -o newinstance,mode=620,ptmxmode=666,gid=5 devpts "$root/dev/pts"
ln -s pts/ptmx "$root/dev/ptmx"
ln -s /proc/self/fd "$root/dev/fd"
rm -f "$root/etc/resolv.conf"
cp -L /etc/resolv.conf "$root/etc/resolv.conf"
install -Dm644 /etc/ssl/certs/ca-certificates.crt "$root/etc/ssl/certs/ca-certificates.crt"
install -Dm644 "$src/ubuntu.sources" "$root/etc/apt/sources.list.d/ubuntu.sources"
printf '#!/bin/sh\nexit 101\n' >"$root/usr/sbin/policy-rc.d"
chmod 755 "$root/usr/sbin/policy-rc.d"
export DEBIAN_FRONTEND=noninteractive LC_ALL=C
test "$(chroot "$root" dpkg --print-architecture)" = armhf
# Ubuntu Base excludes message catalogs. Retain the desktop language before unpacking Xfce.
printf '%s\n' 'path-include=/usr/share/locale/zh_CN/LC_MESSAGES/*.mo' >"$root/etc/dpkg/dpkg.cfg.d/zz-histb-zh-cn"
chroot "$root" apt-get -o APT::Update::Error-Mode=any -o Acquire::Retries=3 update
mapfile -t packages < "$src/packages.txt"
chroot "$root" apt-get -o Acquire::Retries=3 install -y --no-install-recommends "${packages[@]}"
bash "$src/install-overlay.sh" "$root"
chroot "$root" locale-gen zh_CN.UTF-8
chroot "$root" update-locale LANG=zh_CN.UTF-8 LANGUAGE=zh_CN:zh
bash "$src/configure-image-ssh.sh" "$root" "${HISTB_SSH_PUBKEY:-}"
bash "$src/board/build.sh" "$HISTB_SDK" "$out"
install -Dm755 "$out/hifb-plane" "$root/usr/local/sbin/hifb-plane"
install -Dm755 "$out/sink-mode" "$root/usr/local/sbin/sink-mode"
install -Dm755 "$src/board/monitor-mode.py" "$root/usr/local/libexec/histb/monitor-mode.py"
install -Dm755 "$src/board/ram-root.sh" "$root/usr/local/sbin/histb-ram-root"
chroot "$root" useradd -m -s /bin/bash -G audio,video,plugdev desktop
chroot "$root" update-alternatives --install /usr/bin/x-www-browser x-www-browser /usr/local/bin/histb-chromium 100
chroot "$root" update-alternatives --set x-www-browser /usr/local/bin/histb-chromium
kernel=4.4.35_ecoo_81082668
module="$src/board/wifi/rtl8188fu.ko"
echo "43330f5bdcf00fbd539975c9029c2872ac639d54a87216bc3e63e20dddac342c  $module" | sha256sum -c -
install -Dm644 "$module" "$root/usr/lib/modules/$kernel/kernel/drivers/net/wireless/rtl8188fu.ko"
install -m644 "$src/board/wifi/modules.builtin" "$root/usr/lib/modules/$kernel/modules.builtin"
printf 'kernel/drivers/net/wireless/rtl8188fu.ko\n' >"$root/usr/lib/modules/$kernel/modules.order"
chroot "$root" depmod -a "$kernel"
ln -s /srv/tf/desktop "$root/root/TF"
ln -sfn /usr/share/zoneinfo/Asia/Shanghai "$root/etc/localtime"
printf 'Asia/Shanghai\n' > "$root/etc/timezone"
chroot "$root" systemctl disable systemd-networkd-wait-online.service systemd-networkd.socket systemd-networkd.service
chroot "$root" systemctl enable systemd-resolved systemd-timesyncd NetworkManager bluetooth lightdm hifb-plane histb-monitor.service histb-monitor.timer
chroot "$root" systemctl set-default graphical.target
chroot "$root" apt-get check
chroot "$root" dpkg --audit > "$out/dpkg-audit.txt"
test ! -s "$out/dpkg-audit.txt"
chroot "$root" dpkg-query -W -f='${binary:Package}\t${Version}\t${Architecture}\n' > "$out/packages.tsv"
chroot "$root" apt-get clean
find "$root/var/lib/apt/lists" -type f -delete
rm -f "$root/usr/sbin/policy-rc.d" "$root/etc/resolv.conf"
ln -s /run/systemd/resolve/stub-resolv.conf "$root/etc/resolv.conf"
: > "$root/etc/machine-id"
ln -sfn /etc/machine-id "$root/var/lib/dbus/machine-id"
umount -R "$root/dev"
umount "$root/run"
umount "$root/proc"
# Runtime devtmpfs will populate /dev. Supply the two nodes needed for early init.
mkdir -p "$root/dev"
[[ -e "$root/dev/console" ]] || mknod -m600 "$root/dev/console" c 5 1
[[ -e "$root/dev/null" ]] || mknod -m666 "$root/dev/null" c 1 3
mkdir -p "$root/opt"
if [[ -n ${HISTB_CHROMIUM_RUNTIME:-} ]]; then
    browser=$(realpath "$HISTB_CHROMIUM_RUNTIME")
    test -s "$browser/etc/chromium-runtime-packages.tsv" && test -u "$browser/usr/lib/chromium/chrome-sandbox"
    if mountpoint -q "$browser/dev" || mountpoint -q "$browser/proc"; then echo "Runtime build is still mounted" >&2; exit 1; fi
    cp -a "$browser" "$root/opt/histb-chromium"
else
    bash "$src/build-browser.sh" "$root/opt/histb-chromium"
fi
du -sx --block-size=1 "$root" > "$out/installed-size.txt"
tar --numeric-owner --xattrs --xattrs-include='*' --acls -C "$root" -cpf - . |
    gzip -1 > "$out/ubuntu-noble-armhf-xfce.tar.gz.partial"
mv "$out/ubuntu-noble-armhf-xfce.tar.gz.partial" "$out/ubuntu-noble-armhf-xfce.tar.gz"
bash "$src/pack-tar-helper.sh" "$root" "$out"
(cd "$out"; sha256sum ubuntu-noble-armhf-xfce.tar.gz tar-helper.tar.gz packages.tsv > SHA256SUMS)
echo "Root tree: $root"
