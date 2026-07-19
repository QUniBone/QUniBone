# QBone Web API

The `demo` application serves this API when started with `--web [port]`
(default port 80). All request and response bodies are JSON unless noted.
Errors use HTTP status codes with a body of `{"error": "message"}`.

With `WEBUI_PASSWORD` set in the environment, every request requires HTTP
basic auth: any user name, the password must match. Browsers replay the
credentials on the WebSocket handshakes. Unset, access is open.

## State

### `GET /api/state`

Identifies the bridge.

```json
{"platform": "QBUS", "api_version": 0}
```

## Devices and parameters

Devices are the emulated hardware — controllers, drives, serial lines.
Infrastructure singletons (bus adapter, panel driver) are not exposed.

### `GET /api/devices`

Snapshot of every device and its parameters.

```json
[{"name": "RL11", "type": "RLV12_c", "enabled": false, "parent": null,
  "params": [
    {"name": "address", "shortname": "addr", "type": "unsigned",
     "value": 63944, "base": 8, "bitwidth": 18,
     "readonly": false, "info": "controller address"},
    ...]}]
```

Parameter `type` is one of `string`, `bool`, `unsigned`, `unsigned64`,
`double`. Unsigned parameters carry `base` (usually 8) and `bitwidth`.
Drives reference their controller through `parent`.

### `PUT /api/devices/<device>/params/<param>`

Sets a parameter. Device and parameter names are case-insensitive;
`<param>` accepts the full name or the short name.

```json
{"value": "174400"}
```

The value is parsed exactly as the CLI's `p <param> <value>` command
parses it; `enabled` is switched like the CLI's `en`/`dis`. Responds with
the parameter (as in the snapshot) after the write. Validation failures
respond `422` with the device's message. Attaching a disk image is a
write to the drive's `image` parameter; an empty value detaches.

## Bus control

### `POST /api/control`

```json
{"action": "init"}
```

Actions: `init` (pulse bus INIT), `powercycle` (simulated DCOK/POK power
fail cycle), `halt` / `continue` (QBUS HALT line).

## Disk images

Image files live in `$QUNIBONE_DIR/images/`.

### `GET /api/images`

```json
[{"name": "rt11v53.rl02", "path": "/home/.../images/rt11v53.rl02",
  "size": 10485760, "mtime": "2026-07-16 20:51", "attached": ["rl0"]}]
```

`attached` lists the drives whose `image` parameter points at the file.

### `POST /api/images`

Multipart upload (`multipart/form-data`, one file field). The client-side
file name becomes the image name; names must be plain file names.

### `GET /api/images/<name>`

Downloads the image.

### `DELETE /api/images/<name>`

Removes the image file. Refused with `409` while the image is attached to
a drive or referenced by a saved configuration.

## Configurations

A configuration is a named JSON snapshot of the device setup — every
device's enabled state and writable parameter values — stored in
`$QUNIBONE_DIR/configs/<name>.json`.

### `GET /api/configs`

```json
[{"name": "rt11", "mtime": "2026-07-16 20:52", "enabled": ["RL11", "rl0"]}]
```

### `GET /api/configs?current=1`

The live setup in the same shape a saved snapshot has, so a caller can compare
it against the saved ones and tell which — if any — is the configuration
currently loaded. Answers `503` while the machine is busy, the snapshot being a
status query that gives up rather than waiting on the device registry.

### `GET /api/configs/<name>`

The full snapshot:

```json
{"devices": [{"name": "RL11", "enabled": true,
              "params": {"address": "174400", ...}}, ...]}
```

### `PUT /api/configs/<name>`

Saves the current setup under `<name>` (no request body).

### `POST /api/configs/<name>/apply`

Restores the snapshot. Parameters are applied in stored order (controllers
before their drives), unchanged values are skipped, rejections are
collected:

```json
{"ok": true, "errors": []}
```

### `DELETE /api/configs/<name>`

Removes the snapshot.

## WebSockets

### `/ws/events`

Text frames, one JSON event each, pushed to every connected client:

| event | payload |
|---|---|
| `{"t":"param","dev":…,"param":…,"value":…}` | committed parameter change (includes enable/disable, image attach, panel lamps) |
| `{"t":"log","level":n,"label":…,"text":…}` | log message; levels 1 FATAL … 5 DEBUG |
| `{"t":"state","halt":…,"leds":[…],"switches":[…],"init":…,"dcok":…,"pok":…}` | activity LEDs, DIP switches, HALT, bus INIT/DCOK/POK — published on change (10 Hz poll); a full snapshot opens every connection |

### `/ws/console/0`, `/ws/console/1`

Binary frames, byte-transparent in both directions, bridged to the DL11
SLUs: `0` is the PDP-11 console at 777560, `1` the second line at 776500.
No echo, no line discipline; terminal emulation is the client's job. The
physical UART stays attached; the WebSocket is a parallel tap.
