#!/bin/sh
set -eu
cookie=/run/histb-chromium-display/Xauthority
umask 077
xauth -f /root/.Xauthority extract - :0 >"$cookie"
test -s "$cookie"
chown desktop:desktop "$cookie"
