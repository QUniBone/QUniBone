# Console

**Status:** Gathering

The serial console: reliable startup, server-side history replayed to new
connections, and its scope within the dashboard.

## Current state

The console is bridged over WebSockets — `/ws/console/ext` for the 11/73's real
SLU on `/dev/ttyS2`, `/ws/console/0` and `/ws/console/1` for emulated DL11s. The
bridge broadcasts to all connected clients, so several viewers coexist. A client
receives only the bytes that arrive after it connects; output produced before it
connected is gone. Opening the dashboard mid-session shows an empty console until
the next byte. The console sometimes fails to come up correctly, showing no
cursor. Navigating to another page and back sometimes leaves the console
disconnected — a black screen with no cursor — because the WebSocket is torn down
on navigation and does not cleanly re-establish.

## Requirements

### Startup reliability

- The console comes up correctly every time. It does not land in the broken state
  where no cursor is shown.
- Navigating away from the console page and back reconnects it cleanly — no black
  screen, no lost cursor. Replaying the stored history (below) restores the
  screen on reconnect. Relates to web navigation
  ([web-navigation.md](web-navigation.md)).

### History storage and replay

- The console output is **stored on the server** in an **in-memory ring buffer**
  (bounded; not persisted across a service restart).
- The stored history is **raw bytes**. On connect, a client is served the raw
  history verbatim before the live stream, and xterm.js reconstructs the screen.

### Scope

- The console does **not** need to be reconfigurable from the dashboard. Its
  source stays fixed (`ttys2` on this board); the dashboard presents it, it does
  not configure it.
- The console is also reachable at its **own standalone URL** — a bare
  full-screen page separate from the dashboard
  ([web-navigation.md](web-navigation.md)).

## Open questions

### Startup reliability

- What triggers the no-cursor state — the WebSocket connecting before the bridge
  is ready, an xterm.js init race, or a missing initial redraw from the guest?
- Is the console component fully unmounted on page switch (WebSocket closed,
  xterm disposed), or kept alive and just hidden? That decides whether the fix is
  clean teardown+reconnect or keeping it mounted.
- Is it reproducible on a fresh page load, or only after a reconnect?
- Would replaying stored history on connect (below) mask or fix it, or are they
  independent?

### History storage and replay

- How big is the ring — a byte cap, a line cap, or sized to one 24-line screen
  plus some scrollback?
- Which streams get history — the external console, the emulated DL11s, the
  serial-mux ports ([serial-ports.md](serial-ports.md)), or all uniformly?
- On reconnect, full retained history every time, or a resume point so a brief
  drop does not replay everything?
- Relationship to the journal — the console flush already keeps the journal live;
  is the stored console log the same artifact or separate?
