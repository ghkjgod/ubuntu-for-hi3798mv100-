#!/bin/bash
# Fetch the three armhf files pinned from the Raspberry Pi OS signed Bookworm index.
set -euo pipefail
here=$(cd "$(dirname "$0")" && pwd)
dest=$(realpath -m "${1:?usage: prepare.sh OUTPUT_DIRECTORY}")
mkdir -p "$dest"
base=https://archive.raspberrypi.com/debian/pool/main/c/chromium
version='156.0.8078.79-1~deb12u1+rpt1'
tmp=$(mktemp -d "$dest/.download.XXXXXXXX")
trap 'rm -rf -- "$tmp"' EXIT
while read -r sha file; do
    if [[ -f $dest/$file ]] && (cd "$dest" && echo "$sha  $file" | sha256sum -c -); then continue; fi
    curl --fail --location --retry 3 -o "$tmp/$file" "$base/${file%.deb}_${version}_armhf.deb"
    (cd "$tmp" && echo "$sha  $file" | sha256sum -c -)
    mv -fT "$tmp/$file" "$dest/$file"
done <"$here/SHA256SUMS"
