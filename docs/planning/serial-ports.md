# Serial ports over TCP

**Status:** Gathering

Additional emulated serial ports, each mapped to a TCP port so a host program
can connect to it.

## Current state

The 11/73's console SLU is real hardware bridged over `/dev/ttyS2` and carried on
the `/ws/console/ext` WebSocket. QBone can emulate DL11s (777560, 776500) exposed
on `/ws/console/0` and `/ws/console/1`, but this board leaves them disabled. There
is no way to attach an emulated serial line to a TCP endpoint.

## Requirements

- QBone can present **additional emulated serial ports** via a **serial mux**.
- Each port is **mapped to a TCP endpoint** speaking **Telnet with RFC2217**, so
  a client can query and set line parameters (baud, bits). Per line, QBone either
  **listens** (a host program dials in) or **connects out** to a host-provided
  address — the role is configurable per line.
- The mux gets a **dashboard widget**: an additional terminal with tabs, one per
  port ([web-dashboard.md](web-dashboard.md)).

## Decisions

- **Per-line TCP role is configurable** — each line is either listen or
  connect-out.
- **Telnet / RFC2217** framing, so clients can negotiate line parameters.

## Open questions

- Which mux is emulated (DZ11, DZV11, DHV11, …), and does that fix the port count
  and register/vector layout?
- How many ports, and are their bus addresses/vectors fixed or configurable?
- RFC2217 carries line parameters — does the emulated mux act on them (real baud
  pacing), or accept and ignore them since the TCP link has no real UART timing?
- How does a serial port's TCP mapping appear in the configuration model and the
  web UI — is it a device parameter?
- Relationship to the existing `/ws/console/<n>` WebSocket taps — do these new
  ports also get a WebSocket view, or only TCP?
- Single client per port or multiple concurrent clients?
