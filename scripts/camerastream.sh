
#!/usr/bin/env bash
set -euo pipefail

CAMERA="${1:-/dev/video0}"
HOST="${2:-127.0.0.1}"
PORT="${3:-5600}"

gst-launch-1.0 -e \
  v4l2src device="$CAMERA" do-timestamp=true \
  ! videoconvert \
  ! videoscale \
  ! videorate \
  ! video/x-raw,format=I420,width=1280,height=720,framerate=30/1 \
  ! x264enc \
      tune=zerolatency \
      speed-preset=ultrafast \
      bitrate=4000 \
      key-int-max=30 \
      bframes=0 \
  ! h264parse \
  ! rtph264pay pt=96 config-interval=-1 mtu=1200 \
  ! udpsink host="$HOST" port="$PORT" sync=false async=false

