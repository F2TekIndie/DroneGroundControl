# DGC — Drone Ground Control

**Status: proof-of-concept prototype.** This project is intended for exploration
and testing. Features, APIs, and protocols are incomplete and may change; it is
not production-ready.

Qt 6/QML desktop application for receiving drone video and displaying a map.
The current development target is Fedora 44 with system Qt 6.11.

## Current status

Implemented:

- H.264 RTP/UDP reception on port 5600, payload type 96.
- OpenGL video display through GStreamer `qml6glsink`.
- Start/Stop controls, bus error handling, and buffer counters.
- Local color-bar testing and application-window snapshots.
- An OpenStreetMap view with pan and zoom controls.

`DroneManager` and the drone list are scaffolding. Server communication,
telemetry display, and drone commands are not implemented yet.

## Related repositories

Keep the repositories in this sibling layout; CMake uses relative paths to
include the shared SDK:

```text
DroneGroundControl/
├── DGC/
├── DGCServer/
└── DroneSDK/
```

- [DGCServer](../DGCServer/README.md): server and simulator scaffolding.
- [DroneSDK](../DroneSDK/README.md): shared messages and C++ library scaffolding.

## Dependencies

- A C++23 compiler and a CMake version compatible with the chosen Qt toolchain
  (the project declares CMake 3.20 as its minimum).
- Qt 6.11 or later: Core, Gui, Qml, Quick, Location, and Positioning, including
  the Qt Quick Controls and Layouts QML modules.
- Boost.System and threading support.
- Protocol Buffers compiler and development library.
- pkg-config and GStreamer development files, version 1.22 or later.
- GStreamer runtime elements: `udpsrc`, `rtpjitterbuffer`, `rtph264depay`,
  `h264parse`, `openh264dec`, `queue`, `videoconvert`, `identity`, `glupload`,
  `glcolorconvert`, `qml6glsink`, and `videotestsrc` for local tests.

Use matching Qt libraries and the GStreamer Qt plugin. On Fedora, building with
the system Qt kit avoids mixing Fedora's `qml6` plugin with an incompatible Qt
SDK. The plugin depends on Qt private APIs.

## Build

From this repository, with the matching Qt toolchain selected:

```sh
cmake -S . -B build/local -DCMAKE_BUILD_TYPE=Debug
cmake --build build/local --parallel
```

Protocol Buffer sources are generated automatically from the sibling SDK.
Use a fresh build directory when switching Qt kits.

## Run

```sh
./build/local/dgc
```

Run the existing transmitter separately, then click **Start**. The application
does not launch or modify the transmitter.

The receiver expects:

```text
application/x-rtp,media=video,encoding-name=H264,payload=96,clock-rate=90000
UDP port: 5600
```

Stop any other receiver using port 5600 before starting reception here.
For a remote sender, ensure packets target this machine and that the firewall
allows the traffic.

### Camera test scripts

The included transmitter defaults to `/dev/video0`, `127.0.0.1`, and port 5600:

```sh
bash scripts/camerastream.sh
```

Optional arguments are camera device, destination host, and destination port.
This script requires `x264enc` and access to the camera device. It sends
1280×720 video at 30 FPS.

The standalone receiver can be used instead of the application to check the
stream:

```sh
bash scripts/streamdecoder.sh
```

Its optional arguments are UDP port and jitter-buffer latency in milliseconds.
Do not run it concurrently with the application receiver on the same port.

## Diagnostics

Test the same GL display path without a camera or network stream:

```sh
./build/local/dgc --test-video --start
```

Other options:

| Option | Behavior |
| --- | --- |
| `--start` | Start reception automatically. |
| `--test-video` | Use local color bars when reception starts. |
| `--snapshot FILE` | Save the application window after two seconds and exit. |
| `--help` | Show command-line help. |

For example:

```sh
./build/local/dgc --test-video --start --snapshot preview.png
GST_DEBUG=2 ./build/local/dgc
```

Counters reset for each receiver session:

- No UDP packets: check the sender, destination, port, and firewall.
- Packets increase but decoded frames do not: check RTP caps, H.264 decoding,
  and whether a keyframe has arrived.
- GL sink buffers increase but the image is blank: investigate GL upload and
  rendering. Buffer counts alone do not prove visible output.

Counters are cumulative and are not a connection-health or frame-rate monitor.

## Video implementation

```text
UDP → jitter buffer → RTP depayloader → H.264 parser → OpenH264 decoder
    → bounded leaky queue → RGBA conversion → allocation barrier
    → GL upload → GL conversion → QML video item
```

The receiver uses a 100 ms jitter buffer. The decoded-frame queue holds at most
three queued buffers and drops older queued buffers when full. The allocation
barrier (`identity drop-allocation=true`) prevents downstream allocation
requests from reaching the CPU conversion path; this avoids the EGL upload
failure encountered during development.

GStreamer owns frame buffers and textures. There is no frame history or video
recording. Audio is not supported. The UDP port and payload type are currently
fixed in the source. Map tiles require provider access unless already cached.
