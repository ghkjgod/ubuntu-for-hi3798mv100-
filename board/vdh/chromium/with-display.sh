#!/bin/bash
# Each browser invocation holds the display until its process exits.
set -euo pipefail
service=histb-chromium-display.service
lock=/run/lock/histb-chromium-display.lock
cleanup() {
    flock -u 9
    if flock -xn 9; then systemctl stop "$service"; fi
}
exec 9>"$lock"
flock -s 9
trap cleanup EXIT
if systemctl start "$service"; then
    for ((i=0; i<150; i++)); do
        if [[ -S /run/histb-chromium-display/wayland-vdh ]]; then
            HISTB_WEBVIDEO_SESSION=1 /usr/local/bin/histb-chromium "$@" 9>&-
            exit $?
        fi
        systemctl is-active --quiet "$service" || break
        sleep .1
    done
fi
echo "The video display could not start. Opening the X11 browser." >&2
HISTB_CHROMIUM_X11=1 /usr/local/bin/histb-chromium "$@" 9>&-
