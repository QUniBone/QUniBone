# VCB01 (QVSS) emulation — work status

Emulating a DEC VCB01 / QVSS monochrome video subsystem on QBone for the real
11/73, rendering to a remote X display. This file is the resume point after
suspending for hardware tests.

## What works

- **The device** (`10.02_devices/2_src/vcb01.{cpp,hpp}`, `vcb01_render.*`,
  `vcb01_input.*`): registers, CSR (read-only bank field), 6845 CRTC, 8-source
  interrupt controller, SCN2681 DUART (LK201 keyboard on chan A, VSXXX mouse on
  chan B), VSYNC, refresh worker, CRTC-derived height with window resize.
- **The memory API** (`10.05_web/2_src/webapi.cpp`, committed): `GET`/`POST
  /api/memory` reads and writes bus memory by DMA, reliably, CPU running or
  halted. Two fixes made it dependable:
  - a persistent, address-space-sized buffer under a dedicated mutex — a stale
    PRU DMA completion memcpy's into the buffer *after* the request returns, so
    a local buffer got corrupted/crashed;
  - `read_json_body` reads the full body (was capped at 4096 bytes, which
    truncated bulk writes to HTTP 400).
  - Verified again this session: `0→0`, `668→668`, `5349→5349` round-trip clean.
- **The test card** (`tools/vcb01_testcard.py`, committed, in the repo so it can
  be run by hand): DMAs a full-screen picture (square grid, border, solid
  diagonals, crosshair) straight into video memory via `/api/memory` with **no
  PDP-11 program running** — QBone is bus master. This is the reliable way to
  paint the screen and it produces a clean image. Layout constants:
  `XSIZE=1024 YSIZE=864`, 128 bytes/line, scanline map at byte `0o774000`
  (longword `0xFE00`) of the bank, CSR at `0o17777200`, video bank base
  `0o16000000`.

## What's unfinished: `vcbdemo.mac`

Standalone, OS-less demo that brings the board up from a halted CPU, sets up the
MMU itself, draws the pattern, and then reads keyboard + mouse in a loop. Lives
in `qbus-front-panel/systems/rsx11mplus/vcb01-src/vcbdemo.mac` (a working copy
with debug markers is in the job tmp dir).

Status: gets through MMU-on (marker 1), CRTC (2), scanline-map load (3), then
**stops at the first `call mapline` in the bitmap-clear loop** — marker 770
("mapline returned") never set, and the trap handler (which records the faulting
PC and APR6) is not triggered. So it's a genuine stop, not slowness, and not a
*caught* trap.

### The big correction (important — don't re-chase this)

A long investigation concluded that "a `jsr`/`call` double-faults whenever APR6
maps the video bank." **That was wrong — it was an artifact of my own test
harness.** Successive test programs left **stale trap vectors** in low memory;
newer programs didn't reset them, so a benign path jumped to leftover handler
code and looked like a trap. The tell was a stuck marker value of `8` (= octal
`10`, the old illegal-instruction handler's marker).

After clearing low memory (vectors) before each run and installing a fresh
handler on **all** fault vectors (4, 10, 14, 20, 250), a `call` loop with APR6
pointing at the **video bank** runs to completion with no fault. Confirmed the
same for APR6 mapping identity RAM and non-identity RAM. **Calling subroutines
while APR6 maps video memory is fine.** The KDJ11 cache (Force-Miss via CCR at
`0o17777746`) was ruled out as irrelevant along the way.

### Where the real bug hunt resumes

`vcbdemo.mac` itself only installs vectors 4 and 250. If the clear loop takes an
unhandled trap on another vector (10/14/20), the PC goes to a zeroed vector →
HALT at 0, which matches the symptom (stops, no handler fires). But the
equivalent call loop works in the clean harness, so either:

1. there is a real bug in `mapline`/the clear loop specific to vcbdemo, or
2. vcbdemo needs the same all-vector handler + clean low memory the harness has.

Next step (was in progress): reproduce vcbdemo's **map-load + clear-loop**
inside the rigorous harness (clean vectors, all-vector handler, fine-grained
markers before/after the first `mapline`, after the first line clear) to
localize the stop. Likely fix: install handlers on all fault vectors in
vcbdemo and/or find the offending instruction in the clear loop.

Note: `vcbdemo.mac` already carries a fix for the MACRO-11 **no-precedence**
trap — `#vidbase+<760000/100>` must use `<...>` grouping, since MACRO-11
evaluates strictly left-to-right with no operator precedence.

## Board / workflow reference

- Drive the board through the web API (docs in `10.05_web/docs/api.md`); auth is
  HTTP basic, password in `~/.qbone-pw`, any user name.
- Console is the 11/73's on-board SLU on the bone's `/dev/ttyS2`, carried by
  `/ws/console/ext`. DL11 stays disabled. Micro-ODT is reachable there; register
  examine (`R7/`) did **not** work this session — use memory markers instead.
- Standalone-program test cycle used this session:
  1. clear low memory `0..0o777` via `/api/memory` (kills stale vectors),
  2. DMA-load the assembled `.lst` via `tmp/dmaload.py`,
  3. `halt` → `continue` (release BHALT) → `1000G` (via `tmp/odt.py` over the
     console WebSocket) — **must `continue` before `1000G`**,
  4. read progress markers back with `/api/memory`.
- MMU facts: kernel APRs 0–7 identity, APR7→I/O page (`0o177600`), MMR3 bit 4 =
  22-bit, MMR0 bit 0 = enable. Video bank at `0o16000000` sits above the 2 MB
  RAM card, which is why the ROM reports "Memory is Non contiguous" when the
  VCB01 is enabled (verified by toggling the device).
- The 11/73 answers across the whole 22-bit space with a 4 MB card, so an
  emulated device needing a bus window collides until the memory card is made
  smaller; the machine currently has a 2 MB card at 0.

## Scratch files (job tmp dir, not in the repo)

`dmaload.py` (DMA-load a `.lst`), `odt.py` (send ODT over the console
WebSocket), `mmtest.mac` (isolation harness), `vcbdemo.mac` (working copy with
markers), `fill_screen.py` (early test-card precursor to `vcb01_testcard.py`).
