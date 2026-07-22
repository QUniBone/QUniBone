# Working on QUniBone

## The board

The development QBone is `qbone` (`qbone.huebner.org`), user `hans`, with
passwordless sudo and key-based ssh. The PDP-11 it sits in is an 11/73.

## Driving it: use the web API

The emulator runs as `qbone.service` (`/usr/bin/qbone`), serving the web
interface and API on port 80. **Drive the board through that API.** The API is
documented in `10.05_web/docs/api.md` — read it rather than guessing endpoints.

Authentication is HTTP basic, any user name, password in `~/.qbone-pw` on the
workstation:

    curl -s -u ":$(cat ~/.qbone-pw)" http://qbone/api/devices

Requests without it answer `401`.

The service applies `settings.json` on startup, which is where the console
bridge and other machine settings live. Stopping it to run `qbone-demo` by hand
loses that, so the interactive menu is for hardware-level work the API does not
cover, not for ordinary device configuration.

State lives in `/var/lib/bone`: `images/` for disk images, `configs/` for saved
device snapshots, `settings.json` for board settings.

Useful endpoints, all under `http://qbone/api`:

| | |
|---|---|
| `GET /devices` | every device and its parameters |
| `PUT /devices/<dev>/params/<param>` | `{"value": "..."}`; `enabled` switches the device |
| `POST /control` | `{"action": "powercycle"}`, also `init`, `halt`, `continue` |
| `GET /configs`, `POST /configs/<name>/apply` | saved device snapshots |
| `GET /images`, `POST /images` | disk images |

## The console

**The 11/73 has an on-board console SLU, wired to the bone's `/dev/ttyS2`.**
The console is therefore real hardware, and the WebSocket that carries it is
`/ws/console/ext` — the raw tty bridge, with no emulated device behind it. Read
it with `websocat`:

    AUTH=$(printf ":%s" "$(cat ~/.qbone-pw)" | base64)
    websocat --binary -H="Authorization: Basic $AUTH" ws://qbone/ws/console/ext

The bridge is enabled by `external_console` in `PUT /api/settings`, whose
`source` is `ttys2`, `webserial` or `off`. It must stay `ttys2` on this board.

`/ws/console/0` and `/ws/console/1` tap QBone's *emulated* DL11s at 777560 and
776500 instead, which this machine does not use for its console.

**Leave DL11 disabled.** Its `serialport` parameter is `ttyS2`, so enabling it
both duplicates the CPU's own SLU at 777560 and fights the external-console
bridge for the port. `211bsd.json` lists DL11 as enabled, but applying it
silently leaves the device off because the bridge already holds `ttyS2` —
which is the only reason that configuration works.

Nothing else may hold `/dev/ttyS2`: a stray reader steals the bytes and makes
the bridge fail to open with "Another process has locked the comport".

## Building

`./crossbuild.sh` builds for the board in Docker; `-d` deploys the binary.
The builder image is Debian trixie, the same distribution the appliance image
carries, and its tag is a hash of the recipe in the script, so editing the
recipe builds a new image rather than reusing the old one.

Two linker settings that have to stay:

- **Dynamic, never `-static`.** glibc loads its name service backends with
  `dlopen()` even from a static binary, and picks up the ones belonging to the
  glibc the board runs rather than the one the binary carries. A static ARM
  binary built on glibc 2.36 dies with SIGFPE inside `getaddrinfo()` on the
  bone, which runs 2.41. The package names its libraries in `Depends`, written
  by `packaging/build-deb.sh` - `packaging/debian/control` is documentation,
  since the build uses plain `dpkg-deb` and substitutes no `${shlibs:Depends}`.
- **`-no-pie`.** The vendored `libprussdrv.a` holds no position-independent
  code, so a toolchain defaulting to PIE rejects its relocations.

## Hardware notes

The 11/73 answers across the whole 22-bit space — a full 4 MB of memory. Any
emulated device that needs a window in bus address space collides with it until
the memory card is reconfigured.
