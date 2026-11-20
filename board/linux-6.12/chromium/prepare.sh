#!/bin/sh
set -eu
here=$(CDPATH='' cd -- "$(dirname -- "$0")" && pwd)
target=${1:?provide the package staging directory}
mirror=${DEBIAN_MIRROR:-https://mirrors.tuna.tsinghua.edu.cn/debian}
mkdir -p "$target"
while read -r hash file; do
    case "$file" in
        libegl-mesa0_*) pool=pool/main/m/mesa ;;
        libegl1_*|libgles2_*) pool=pool/main/libg/libglvnd ;;
        *) exit 1 ;;
    esac
    if [ -f "${CHROMIUM_EGL_CACHE:-/nonexistent}/$file" ]; then
        cp "$CHROMIUM_EGL_CACHE/$file" "$target/$file"
    else
        curl -fsSL --connect-timeout 15 --retry 3 "$mirror/$pool/$file" -o "$target/$file"
    fi
    printf '%s  %s\n' "$hash" "$target/$file" | sha256sum -c -
    chmod 0644 "$target/$file"
done <"$here/SHA256SUMS"
install -m644 "$here/SHA256SUMS" "$target/SHA256SUMS"
