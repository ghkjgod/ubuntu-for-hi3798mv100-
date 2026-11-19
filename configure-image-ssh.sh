#!/bin/bash
set -euo pipefail
root=$(realpath "${1:?offline Ubuntu root filesystem required}")
[[ $root != / && -x $root/usr/sbin/sshd ]]
# The package postinst creates host keys in the build environment.
rm -f -- "$root"/etc/ssh/ssh_host_*_key "$root"/etc/ssh/ssh_host_*_key.pub
systemctl --root="$root" disable ssh.socket
systemctl --root="$root" enable ssh.service
if [[ -n ${2:-} ]]; then
    key=$2
    [[ $(awk 'NF {n++} END {print n+0}' "$key") == 1 ]]
    ssh-keygen -lf "$key" >/dev/null
    install -d -m 700 "$root/root/.ssh"
    awk 'NF {print $1, $2}' "$key" > "$root/root/.ssh/authorized_keys"
    chmod 600 "$root/root/.ssh/authorized_keys"
fi
