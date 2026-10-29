# DGC — Drone Ground Control

**Status: proof-of-concept prototype.** This project is intended for exploration
and testing. Features, APIs, and protocols are incomplete and may change; it is
not production-ready.

Qt 6/QML desktop application for receiving drone video and displaying a map.
The current development target is Fedora Linux with Qt 6.11 or later.

## Current status

Implemented:

- H.264 RTP/UDP reception on port 5600, payload type 96.
- OpenGL video display through GStreamer `qml6glsink`.
- Start/Stop controls, bus error handling, and buffer counters.
- Local color-bar testing and application-window snapshots.
- An OpenStreetMap view with pan and zoom controls.

`gRPCClient` implements all device, attribute, position and history operations.
It alone owns gRPC channels, stubs and Protobuf conversion; its public interface
uses UUID strings, application data and structured results. There is no SQL or
MQTT access in this application.

`DroneManager` owns a QML-facing list model and submits communication jobs using
`QRunnable` on a dedicated `QThreadPool`. Queued callbacks apply model changes on
the GUI thread. The application requests one refresh on launch. Further refreshes
are explicitly requested through `refreshDrones()`. The list and map panels can
consume this model; their visual contents are still scaffolding. See the
[server startup instructions](../DGCServer/README.md) for the separate
server/simulator/broker workflow.

## Related repositories

Keep the repositories in this sibling layout; CMake uses relative paths to
include the shared SDK:

```text
DroneGroundControl/
├── DGC/
├── DGCServer/
└── DroneSDK/
```

- [DGCServer](../DGCServer/README.md): MQTT simulator, drone cache, SQLite synchronization,
  and gRPC services.
- [DroneSDK](../DroneSDK/README.md): shared Protobuf messages and gRPC service definitions.

## Dependencies

- A C++23 compiler and a CMake version compatible with the chosen Qt toolchain
  (the project declares CMake 3.21 as its minimum).
- Qt 6.11 or later: Core, Gui, Qml, Quick, Location, and Positioning, including
  the Qt Quick Controls and Layouts QML modules.
- Boost.System and threading support.
- Qt Test when `BUILD_TESTING=ON` (the default).
- Protocol Buffers compiler and development library.
- gRPC C++ development files, CMake package configuration, and `grpc_cpp_plugin`.
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
cmake -S . -B build/local -DCMAKE_BUILD_TYPE=Debug -DBUILD_TESTING=ON
cmake --build build/local --parallel
```

Protocol Buffer message classes and gRPC client/server bindings are generated
automatically from the sibling SDK. The application makes its initial server
request on launch through `gRPCClient`. Use a fresh build directory when switching
Qt kits. Set `BUILD_TESTING=OFF` to omit the test target and Qt Test dependency.

## Run

```sh
./build/local/dgc
```

For telemetry, initialize and start DGCServer first, following its
[startup instructions](../DGCServer/README.md). The desktop defaults to client 99
and `127.0.0.1:5600`. It requests the drone list and attributes once on launch;
there is no periodic polling or subscription. The model and invokable API are
available to QML, while the drone-list and map-marker UI is still scaffolding.

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

## Drone communication API

```text
QML ↔ DroneManager ↔ gRPCClient ↔ DGCServer
```

Only `gRPCClient` interacts with gRPC and generated Protobuf types. Its public
interface exposes application structs and `RpcResult<T>` (`std::expected` with
an application error code/message). `DroneManager` submits calls as `QRunnable`
jobs on a dedicated `QThreadPool` with four workers, then applies results through
queued GUI-thread callbacks. Queued and running requests contribute to `_pending`,
which drives the `busy` property.

### Data and model

`Drone` contains identity and metadata. `DroneAttributes` holds state, online
status, and position. Both shared structs, plus `DronePosition`, live in
`src/drone.h`. The manager keeps attribute snapshots separately by UUID,
preserves them when refreshing identities, and removes them with deleted or
unlisted drones.

`DroneRef`, `DroneAttributeChanges`, samples, and structured result/error types
are declared in `grpcclient.h`. Optional fields in `DroneAttributeChanges`
distinguish omitted fields from false, zero, or `(0, 0)` in partial writes and
single-attribute replies. `getAttributes` returns a typed snapshot; the client
rejects replies missing state or online, and tracks whether position is present.

The manager is supplied to QML as `dm`. `dm.drones` exposes these model roles:

| Role | Value |
| --- | --- |
| `droneId` | Canonical lowercase UUID string. |
| `clientId` | Numeric client ID. |
| `name`, `type` | Device metadata returned by the server. |
| `state`, `online` | Current telemetry. |
| `latitude`, `longitude` | Coordinates, unset until a position has been read. |
| `hasPosition` | Whether the model has a position. |

`endpoint` defaults to `127.0.0.1:5600`; `clientId` defaults to 99. Both properties
are writable. Changing either cancels the old client's requests and clears the
model; call `refreshDrones()` to load the new selection. `busy` tracks all pending
jobs, including cancelled jobs until their completion is delivered. `lastError`
contains the most recent failure message.

### Requests and results

Manager invokables are asynchronous and return a request ID. Zero means the
manager has shut down and no request was scheduled. Local input validation can
emit an error immediately. Results are delivered through:

- `operationFinished(requestId, operation, droneId, result)`.
- `operationFailed(requestId, operation, droneId, code, message)`.

Replies contain application/QML maps and lists, not Protobuf objects. Creation
returns device metadata including its new UUID. Successful writes return true.
Error codes include `cancelled`, `timeout`, `unavailable`, `notFound`,
`invalidRequest`, `rejected`, `invalidResponse`, `resourceExhausted`, and `internal`.

| Invokable | Input and behavior |
| --- | --- |
| `refreshDrones()` | Fetch identities for the current client, then their attributes. |
| `createDrone()` | Create under the current client, add the returned identity, and fetch attributes. |
| `deleteDrone(id)` | Delete and remove the identity and attribute snapshot from the model on success. |
| `updateAttributes(id, values)` | Partial map with online bool, state uint32, and/or position map. |
| `getAttribute(id, name)` | Read online, state, or position and merge that field into the model. |
| `getAttributes(id)` | Read and apply the complete attribute snapshot. |
| `updatePosition(id, latitude, longitude)` | Update coordinates. |
| `getPosition(id)` | Read and apply position without resetting other attributes. |
| `addTimeseries(id, samples)` | Send complete coordinate pairs in one batch. |
| `getTimeseries(id, name, timestampMs)` | Read an exact sample, or latest when timestamp is omitted. |
| `getTimeseriesRange(id, name, startMs, endMs)` | Read inclusive optional bounds; empty name selects both coordinate series. |

The current client ID is included with each drone UUID automatically. Attribute
position values use `{latitude, longitude}` maps. History samples use maps with
`name`, `timestampMs`, and `coordinate`; send both latitude and longitude for
each timestamp.

Timestamps are UTC Unix milliseconds. Inputs accept integer values or canonical
decimal int64 strings. Use strings outside JavaScript's exact integer range;
results always expose timestamp strings to preserve precision. Broad history
reads remain subject to the server's unpaginated response and the gRPC receive
limit.

Calls and manual refreshes may overlap, including writes to the same drone.
Results are applied as they arrive. Successful attribute, position, and history
updates fetch current attributes for drones already present in the model.
There is no polling timer or per-drone write lock.

### Deadlines, cancellation, and shutdown

The transport has a five-second per-call deadline, configurable through the
`gRPCClient` constructor. Calls share a channel and service stubs, with a separate
context/response per request. The deadline starts when a worker executes the RPC;
queued jobs can wait before that. Mutations are not automatically retried.
Server write success means cache acceptance, not a durable SQL commit.

`cancelRequests()` cancels the current client and creates a replacement while
preserving the model. Active calls are cancelled; queued jobs complete through
the cancelled client without starting another RPC. Completions still update
`_pending` and deliver their success/error signal. Already completed replies can
still be delivered.

`shutdown()` cancels active calls, clears queued jobs, and joins the pool. The
application invokes it before exit, and the destructor also invokes it. Jobs
retain the client while running. Queued delivery is tied to the manager's QObject
lifetime, and callbacks do not modify a shut-down or destroyed manager.

### Verification

```sh
ctest --test-dir build/local --output-on-failure
```

`dgc_rpc_test` uses a local gRPC test server and requires permission to open a
loopback listener. It covers all client methods, conversion, partial updates
including false/zero values, errors, deadlines, cancellation, GUI-thread model
notifications, attribute retention across identity refreshes, pending-request
accounting, and destruction during an active call. It needs no DGCServer process,
SQL database, MQTT broker, or camera. Live video and map rendering are separate
manual checks.
