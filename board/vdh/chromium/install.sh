#!/bin/bash
# Run on the Ubuntu host, after board/vdh/install.sh and the Lima browser setup.
set -euo pipefail
here=$(cd "$(dirname "$0")" && pwd)
launcher="$here/../../../overlay/usr/local/bin/histb-chromium"
packages=$(realpath "${1:?usage: install.sh VERIFIED_PACKAGE_DIRECTORY}")
version='156.0.8078.79-1~deb12u1+rpt1'
base=/opt/histb-webvideo
[[ $(id -u) == 0 && $(uname -r) == 6.12.114-hi3798-mv100-exp && -c /dev/mv100-avc ]]
[[ -x /opt/histb-chromium/usr/bin/chromium && -r /usr/local/lib/histb-lima/libkmsro-lima-select.so && -s $launcher ]]
install -d -m 755 "$base" /etc/histb /usr/local/lib/histb-webvideo
exec 8>/run/lock/histb-webvideo-install
flock 8
(cd "$packages" && sha256sum -c "$here/SHA256SUMS")
if ! command -v weston >/dev/null; then
    apt-get update
    apt-get install --no-install-recommends -y weston
fi
stage=$(mktemp -d "$base/.stage.XXXXXXXX")
trap 'rm -rf -- "$stage"' EXIT
if [[ ! -d $base/$version ]]; then
    install -d -m 755 "$stage/root"
    for name in chromium chromium-common chromium-sandbox; do
        file="$packages/$name.deb"
        [[ $(dpkg-deb -f "$file" Package) == "$name" &&
           $(dpkg-deb -f "$file" Version) == "1:$version" &&
           $(dpkg-deb -f "$file" Architecture) == armhf ]]
        dpkg-deb --threads-max=1 -x "$file" "$stage/root"
    done
    [[ -x $stage/root/usr/lib/chromium/chromium && -u $stage/root/usr/lib/chromium/chrome-sandbox ]]
    mv -T "$stage/root" "$base/$version"
fi
ln -s "$version" "$stage/current"
mv -fT "$stage/current" "$base/current"
# Seed a separate profile for the newer browser and preserve the X11 profile for rollback.
config=/home/desktop/.config
if [[ ! -d $config/chromium-vdh && -d $config/chromium ]]; then
    runuser -u desktop -- bash -eu -c '
        tmp=$(mktemp -d "$1/.chromium-vdh.XXXXXXXX")
        trap '\''rm -rf -- "$tmp"'\'' EXIT
        cp -a --reflink=auto -- "$1/chromium/." "$tmp/"
        rm -f -- "$tmp/SingletonCookie" "$tmp/SingletonLock" "$tmp/SingletonSocket" "$tmp/DevToolsActivePort"
        mv -T "$tmp" "$1/chromium-vdh"
    ' bash "$config"
fi
install -m 755 "$here"/{with-display.sh,display-auth.sh} /usr/local/lib/histb-webvideo/
install -m 644 "$here/histb-chromium-display.service" /etc/systemd/system/
systemctl daemon-reload
install -m 755 "$launcher" /usr/local/bin/.histb-chromium.new
mv -fT /usr/local/bin/.histb-chromium.new /usr/local/bin/histb-chromium
if [[ -f /etc/histb/chromium-vdh.enabled ]]; then
    printf '%s\n' 'Chromium web video installed. The desktop launcher uses Weston.'
else
    printf '%s\n' 'Chromium web video installed. The desktop launcher uses native X11.'
fi
