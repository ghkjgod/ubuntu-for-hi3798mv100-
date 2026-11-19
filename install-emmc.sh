#!/bin/bash
set -euo pipefail
src=$(cd "$(dirname "$0")" && pwd)
usage() { echo "usage: $0 HOST inspect | HOST stage RELEASE 'CID START LENGTH' [TF_UUID] | HOST {trial|rollback} 'CID START LENGTH'" >&2; exit 2; }
[[ $# -ge 2 && $1 =~ ^[a-zA-Z0-9_][a-zA-Z0-9_.@-]*$ ]] || usage
ssh_cmd=(ssh -T -o StrictHostKeyChecking=yes "$1")
action=$2
if [[ $action == inspect && $# == 2 ]]; then
    exec "${ssh_cmd[@]}" 'sh -s -- inspect' < "$src/board/emmc-stage.sh"
fi
case "$action" in
    stage) [[ $# == 4 || $# == 5 ]] || usage; id=$4; out=$(realpath "$3"); uuid=${5:--} ;;
    trial|rollback) [[ $# == 3 ]] || usage; id=$3 ;;
    *) usage ;;
esac
[[ $id =~ ^[a-fA-F0-9]{32}\ [0-9]+\ [0-9]+$ ]] || usage
remote=$("${ssh_cmd[@]}" 'umask 077; mktemp /tmp/noble-emmc.XXXXXX')
[[ $remote =~ ^/tmp/noble-emmc\.[a-zA-Z0-9]+$ ]]
trap '"${ssh_cmd[@]}" "rm -f $remote" </dev/null || true' EXIT
"${ssh_cmd[@]}" "cat > $remote" < "$src/board/emmc-stage.sh"
if [[ $action != stage ]]; then
    "${ssh_cmd[@]}" "sh $remote $action '$id'" </dev/null
    exit
fi
[[ $uuid == - || $uuid =~ ^[a-fA-F0-9-]{36}$ ]] || usage
(cd "$out"; sha256sum -c SHA256SUMS)
read -r zipped unpacked _ < <(gzip -l "$out/ubuntu-noble-armhf-xfce.tar.gz" | tail -1)
[[ $zipped =~ ^[0-9]+$ && $unpacked =~ ^[0-9]+$ && $unpacked -gt $zipped ]]
needed_kib=$(( (zipped + unpacked) / 1024 + 262144 ))
"${ssh_cmd[@]}" "date -u -s @$(date +%s) && sh $remote prepare '$id' $needed_kib" </dev/null
for file in "$out/tar-helper.tar.gz" "$out/ubuntu-noble-armhf-xfce.tar.gz" "$src/board/emmc-init.sh"; do
    name=$(basename "$file")
    hash=$(sha256sum "$file" | cut -d' ' -f1)
    "${ssh_cmd[@]}" "sh $remote receive '$id' $name $hash" < "$file"
done
"${ssh_cmd[@]}" "sh $remote extract '$id' '$uuid'" </dev/null
