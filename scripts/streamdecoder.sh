
#!/usr/bin/env bash
set -euo pipefail

PORT="${1:-5600}"
LATENCY="${2:-30}"

gst-launch-1.0 -v \
  udpsrc port="$PORT" \
    caps="application/x-rtp,media=video,encoding-name=H264,payload=96,clock-rate=90000" \
  ! rtpjitterbuffer latency="$LATENCY" drop-on-latency=true \
  ! rtph264depay \
  ! h264parse \
  ! openh264dec \
  ! videoconvert \
  ! autovideosink sync=false

