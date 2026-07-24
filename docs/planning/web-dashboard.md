# Web: dashboard

**Status:** Gathering

Refinements to the dashboard: the control row, the front panel, disk widgets,
and LED rendering.

## Current state

The dashboard shows status tiles, an 80×24 console, a control row, RL02 disk
panels, and a log. Halt and continue are separate buttons, alongside Pulse INIT
and Power cycle. Run/halt/init state, activity LEDs, and DIP switches sit as
separate top tiles. Disk widgets show a graphical state but no words. LEDs render
with a pronounced glow.

## Requirements

### Control panel — PDP-11/03 console

The control section is rendered as the **PDP-11/03 front-panel bezel** (two LEDs
and three rocker switches), driving QBone's control actions:

- **PWR OK LED** — lit when the machine is powered (DC present).
- **RUN LED** — lit when the CPU is running and executing an instruction; off
  when halted or waiting. This is the run-state display, now living in the
  control section.
- **RESTART switch** (momentary) — resets the machine and restarts the processor.
  Maps to QBone's init/restart. Real function: a system reset before restarting
  execution.
- **HALT / ENABLE switch** (two-position) — HALT halts the processor (into the
  console/ODT state), ENABLE lets it run. It reflects the **live run state** and
  sets it — this is the single halt/continue control (no separate halt and
  continue buttons). Maps to QBone halt / continue.
- **DC ON/OFF switch** — powers the machine on and off; an OFF→ON cycle is a power
  cycle. Maps to QBone powercycle.

The two LEDs are the bus/run display moved into the control section; the
HALT/ENABLE switch is the single run-state toggle.

### Front panel — activity LEDs and DIP switches

- The activity LEDs and DIP switches form a **fixed-size grid**, tightened and
  visually improved, in the same LED visual language as the control panel.
- The **DIP switches are display-only** — they reflect state, not toggled from
  the dashboard.

### Disk widgets

- The RL02 widget, and the other disk widget, gain a **verbal status**:
  `loaded` / `ready` / `busy` / `off` / `idle`.
- **Any disk drive's image can be selected from its widget**, without leaving the
  dashboard. The widget opens the **same image-assignment picker** used on the
  configuration-management screen
  ([web-config-management.md](web-config-management.md)), for consistency.
- No **fixed/removable chip** on the disk widgets.

### Serial mux widget

- Adding a serial mux ([serial-ports.md](serial-ports.md)) adds a dashboard
  widget: an additional terminal with **tabs, one per port of the mux**.

### LED rendering

- **All LEDs use the same rendering** — the status tiles in the top-right and the
  LEDs in the front panel render the same way.
- Slightly **larger** LEDs.
- **Less pronounced glow**.
- LED colors **closer to real LED colors**.

### Chrome

- The "QBUS web interface" caption in the lower-left corner is removed.

## Decisions

- **Control panel modelled on the PDP-11/03 console** — PWR OK / RUN LEDs and
  RESTART / HALT-ENABLE / DC-ON-OFF switches, mapped to QBone's init, halt,
  continue, and powercycle actions. (Reference: the 11/03 bezel photo.)
- **DIP switches display-only.**
- **Any disk drive** can have its image selected from its widget.

## Open questions

- RESTART's exact QBone mapping — is it a bare INIT pulse, or a reset that also
  restarts the CPU from its power-up/boot sequence (INIT + continue)? Check the
  11/03 manual's restart behaviour against QBone's `init`/`continue`.
- HALT/ENABLE switch: it reflects live run state — what does it show when the
  state is momentarily unknown or transitional (disabled, mid-position, spinner)?
- Does the DC ON/OFF switch model a real power state QBone can be in (powered vs.
  not), or is OFF→ON simply the powercycle action with no persistent "off" state?
- The activity-LED / DIP-switch grid is a QBone display with no equivalent on the
  real 11/03 bezel — does it stay as a separate element beside the 11/03-styled
  control panel, or fold in? What do the DIP switches represent, and where does
  their state come from?
- Disk verbal status: exact mapping from device state to each word — what
  distinguishes `ready` from `idle`, and `loaded` from `ready`?
- LED colors: which devices/signals get which colors — the 11/03 photo anchors
  the control-panel LEDs; what about the activity LEDs and status tiles?
- VCB01 keyboard reliability is tracked separately in [vcb01.md](vcb01.md).
