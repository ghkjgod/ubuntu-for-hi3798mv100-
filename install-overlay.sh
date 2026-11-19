#!/bin/bash
set -euo pipefail
src=$(dirname "$(realpath "$0")")
root=$(realpath "$1")
test "$root" != / && test -e "$root/etc/os-release"
while IFS= read -r -d '' entry; do
    rel=${entry#"$src/overlay"}
    if [[ -d $entry ]]; then
        install -d -o root -g root -m 755 "$root$rel"
    else
        mode=644
        [[ $rel != /usr/local/bin/* ]] || mode=755
        [[ $rel != /etc/NetworkManager/system-connections/*.nmconnection ]] || mode=600
        install -o root -g root -m "$mode" "$entry" "$root$rel"
    fi
done < <(find "$src/overlay" -print0)
