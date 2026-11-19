#!/bin/bash
set -euo pipefail
src=$(dirname "$(realpath "$0")")
if [[ $# != 3 ]]; then echo "usage: $0 SSH_ALIAS 'CID NAME SECTORS START LENGTH' TF_FILESYSTEM_UUID" >&2; exit 2; fi
[[ $1 =~ ^[a-zA-Z0-9_][a-zA-Z0-9_.@-]*$ ]]
[[ $2 =~ ^[a-fA-F0-9]{32}\ [[:alnum:]_-]+\ [0-9]+\ [0-9]+\ [0-9]+$ ]]
[[ $3 =~ ^[a-fA-F0-9-]{36}$ ]]
ssh_cmd=(ssh -T -o StrictHostKeyChecking=yes "$1")
"${ssh_cmd[@]}" "sh -s -- mount-installed '$2' '$3'" < "$src/card.sh"
# This command replaces BusyBox init. The previous system's SSH session ends here.
"${ssh_cmd[@]}" "date -u -s @$(date +%s) && sh /tmp/noble-card/usr/local/sbin/histb-ram-root /tmp/noble-card" </dev/null
