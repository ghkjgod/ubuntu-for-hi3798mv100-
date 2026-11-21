#!/usr/bin/env bash
# Compare all frames through both VDH frontends, including delayed B frames at EOS.
set -euo pipefail
size=${1:-320x240}
case "$size" in 160x128|320x240|1280x720|1920x1080) ;; *) exit 2;; esac
tmp=$(mktemp -d)
trap 'rm -rf -- "$tmp"' EXIT
cd "$tmp"
stats=/sys/devices/platform/mv100-vdh
ffmpeg -nostdin -v error -f lavfi -i "testsrc2=size=$size:rate=25" -frames:v 48 \
  -c:v libx264 -profile:v high -x264-params 'threads=1:ref=4:bframes=3:b-pyramid=normal:weightp=2:weightb=1:keyint=24' input.mp4
pixels=(-map 0:v -pix_fmt nv12 -fps_mode passthrough -threads 1 -f rawvideo -)
reference=$(ffmpeg -nostdin -v error -hwaccel none -i input.mp4 "${pixels[@]}" | sha256sum)
for api in request stateful; do
  before=$(cat "$stats/decoded_frames")
  refused=$(cat "$stats/avc_refusals")
  rejected=$(cat "$stats/rejected_frames")
  if [[ $api == request ]]; then
    actual=$(timeout -k 3 90 gst-launch-1.0 -q filesrc location=input.mp4 ! qtdemux ! h264parse ! v4l2slh264dec ! fdsink fd=1 | sha256sum)
  else
    actual=$(timeout -k 3 90 ffmpeg -nostdin -v error -c:v h264_v4l2m2m -i input.mp4 "${pixels[@]}" | sha256sum)
  fi
  frames=$(( $(cat "$stats/decoded_frames") - before ))
  [[ "$actual" == "$reference" && $frames == 48 && $(cat "$stats/avc_refusals") == "$refused" && $(cat "$stats/rejected_frames") == "$rejected" ]]
  printf 'PASS %s %s %s hardware frames; NV12 SHA256 %s\n' "$api" "$size" "$frames" "${actual%% *}"
done
