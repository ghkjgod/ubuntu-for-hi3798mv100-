#!/bin/bash
# SSH_ALIAS is a host entry for the target's isolated NIC and authenticated host key.
set -euo pipefail
src=$(dirname "$(realpath "$0")")
if [[ $# -lt 2 ]]; then
    echo "usage: $0 SSH_ALIAS inspect | {format|resume} OUTPUT_DIR 'CID NAME SECTORS START LENGTH' {I_AUTHORIZE_FORMAT_EXTERNAL_TF_EXISTING_PARTITION_ONLY|FS_UUID}" >&2
    exit 2
fi
host=$1
action=$2
[[ $host =~ ^[a-zA-Z0-9_][a-zA-Z0-9_.@-]*$ ]]
ssh_cmd=(ssh -T -o StrictHostKeyChecking=yes "$host")
if [[ $action == inspect && $# == 2 ]]; then
    exec "${ssh_cmd[@]}" 'sh -s -- inspect' < "$src/card.sh"
fi
[[ $# == 5 && $3 != '' && $4 =~ ^[a-fA-F0-9]{32}\ [[:alnum:]_-]+\ [0-9]+\ [0-9]+\ [0-9]+$ ]]
out=$(realpath "$3")
identity=$4
proof=$5
case "$action" in
    format) [[ $proof == I_AUTHORIZE_FORMAT_EXTERNAL_TF_EXISTING_PARTITION_ONLY ]] ;;
    resume) [[ $proof =~ ^[a-fA-F0-9-]{36}$ ]] ;;
    *) exit 2 ;;
esac
(cd "$out"; sha256sum -c SHA256SUMS)
remote=$("${ssh_cmd[@]}" 'umask 077; mktemp /tmp/noble-card.XXXXXX')
[[ $remote =~ ^/tmp/noble-card\.[A-Za-z0-9]+$ ]]
trap '"${ssh_cmd[@]}" "rm -f $remote" </dev/null || true' EXIT
"${ssh_cmd[@]}" "cat > $remote" < "$src/card.sh"
"${ssh_cmd[@]}" "date -u -s @$(date +%s) && sh $remote $action '$identity' '$proof'" </dev/null
for file in tar-helper.tar.gz ubuntu-noble-armhf-xfce.tar.gz; do
    hash=$(sha256sum "$out/$file" | cut -d' ' -f1)
    "${ssh_cmd[@]}" "sh $remote receive '$identity' $file $hash" < "$out/$file"
done
"${ssh_cmd[@]}" "sh $remote install '$identity'" </dev/null
