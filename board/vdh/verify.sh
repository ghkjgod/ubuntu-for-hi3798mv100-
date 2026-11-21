#!/usr/bin/env bash
# Run on the board after installation, with any other decoder users closed.
set -euo pipefail
size=${1:-320x240}
case "$size" in 320x240|1280x720|1920x1080|1920x1088) ;; *) echo 'Unsupported test size' >&2; exit 2;; esac
tmp=$(mktemp -d)
trap 'rm -rf -- "$tmp"' EXIT
stats=/sys/devices/platform/mv100-vdh
for codec in 264 265; do
    if [[ $codec == 264 ]]; then
        opts=(-profile:v high -x264-params 'threads=1:ref=1:bframes=0:weightp=0:sliced-threads=0:keyint=96')
    else
        opts=(-profile:v main -x265-params 'pools=1:frame-threads=1:wpp=0:weightp=0:weightb=0:bframes=0:ref=1:scenecut=0:keyint=96:log-level=error')
    fi
    ffmpeg -nostdin -v error -f lavfi -i "testsrc2=size=$size:rate=24" -frames:v 24 \
        -c:v "libx$codec" -pix_fmt yuv420p "${opts[@]}" -y "$tmp/input.mp4"
    expected=$(ffmpeg -nostdin -v error -hwaccel none -threads 1 -i "$tmp/input.mp4" \
        -pix_fmt nv12 -threads 1 -f rawvideo - | sha256sum)
    before=$(cat "$stats/decoded_frames")
    rejected=$(cat "$stats/rejected_frames")
    actual=$(gst-launch-1.0 -q filesrc location="$tmp/input.mp4" ! qtdemux ! "h${codec}parse" \
        ! "v4l2slh${codec}dec" ! fdsink fd=1 | sha256sum)
    delta=$(( $(cat "$stats/decoded_frames") - before ))
    printf 'H.%s %s VDH frames=%d SHA256=%s\n' "$codec" "$size" "$delta" "${actual%% *}"
    [[ "$expected" == "$actual" && $delta == 24 && $(cat "$stats/rejected_frames") == "$rejected" ]]
done
