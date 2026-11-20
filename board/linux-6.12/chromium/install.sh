#!/bin/sh
set -eu
runtime=/opt/histb-chromium
[ -x "$runtime/usr/bin/chromium" ] || exit 0
installed() {
    [ "$(chroot "$runtime" dpkg-query -W -f='${db:Status-Abbrev}' "$1" 2>/dev/null)" = 'ii ' ]
}
if installed libegl1 && installed libgles2 && installed libegl-mesa0; then exit 0; fi
archives=/usr/local/share/histb-lima/chromium-egl
(cd "$archives" && sha256sum -c SHA256SUMS)
# The offline packages target the published Bookworm runtime. Let apt resolve a
# newer runtime's own Mesa/GLVND set instead of mixing versions or downgrading.
mesa=$(chroot "$runtime" dpkg-query -W -f='${Version}' libgl1-mesa-dri)
glvnd=$(chroot "$runtime" dpkg-query -W -f='${Version}' libglvnd0)
if [ "$mesa" != '22.3.6-1+deb12u2' ] || [ "$glvnd" != '1.6.0-1' ]; then
    echo 'Install libegl1 libegl-mesa0 libgles2 with apt in the existing Debian Chromium runtime.' >&2
    exit 1
fi
# dpkg reads the package files on the host and maintains the target database.
dpkg --root="$runtime" --install "$archives"/*.deb
