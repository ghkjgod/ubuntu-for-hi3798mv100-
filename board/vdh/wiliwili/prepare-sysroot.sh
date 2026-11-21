#!/bin/bash
# Prepare an isolated, signed Noble armhf build root on an Ubuntu build host.
set -euo pipefail
root=$(realpath "${1:-.}")
sysroot="$root/work/sysroot"
if [[ -e "$sysroot" ]]; then echo "Build sysroot already exists: $sysroot" >&2; exit 1; fi
aptroot=$(mktemp -d "$root/.apt-armhf.XXXXXXXX")
trap 'rm -rf -- "$aptroot"' EXIT
mkdir -p "$aptroot"/{parts,lists/partial,debs/partial,stage/usr/{lib,bin,sbin}}
touch "$aptroot/status"
for dir in lib bin sbin; do ln -s "usr/$dir" "$aptroot/stage/$dir"; done
for suite in noble noble-updates noble-security; do
    echo "deb [arch=armhf signed-by=/usr/share/keyrings/ubuntu-archive-keyring.gpg] https://mirrors.ustc.edu.cn/ubuntu-ports $suite main universe"
done >"$aptroot/sources.list"
cat >"$aptroot/conf" <<EOF
APT::Architecture "armhf";
APT::Architectures { "armhf"; };
Dir::Etc::sourcelist "$aptroot/sources.list";
Dir::Etc::sourceparts "$aptroot/parts";
Dir::State::status "$aptroot/status";
Dir::State::lists "$aptroot/lists";
Dir::Cache::archives "$aptroot/debs";
Dir::Cache::pkgcache "";
Dir::Cache::srcpkgcache "";
Acquire::Languages "none";
EOF
export APT_CONFIG="$aptroot/conf"
apt-get update
apt-get -y --download-only --no-install-recommends install \
    libc6-dev libstdc++-13-dev libmpv-dev libopencc-dev libcurl4-openssl-dev libssl-dev \
    libdbus-1-dev libwebp-dev zlib1g-dev libegl-dev libgles-dev libx11-dev libxrandr-dev \
    libxcursor-dev libxi-dev libxinerama-dev libxkbcommon-dev liblua5.2-dev libasound2-dev
for package in "$aptroot"/debs/*.deb; do dpkg-deb -x "$package" "$aptroot/stage"; done
mv "$aptroot/stage" "$sysroot"
