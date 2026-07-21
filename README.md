# QUniBone

Linux-to-UNIBUS and Linux-to-QBUS bridge for DEC PDP-11 systems. A BeagleBone
Black plugged into a UNIBUS or QBUS backplane emulates peripherals, exercises
devices, and helps diagnose and repair vintage hardware. One codebase serves
both buses — **UniBone** for UNIBUS, **QBone** for QBUS — selected at compile
time via `#define UNIBUS` / `#define QBUS`.

## Heritage

This is the community-maintained mainline of QUniBone, continuing the work of
**Jörg Hoppe** (retrocmp.com: [UniBone](http://retrocmp.com/projects/unibone/),
[QBone](http://retrocmp.com/projects/qbone/)). The original reference codebase
is preserved at
[`QUniBone/QUniBone-classic`](https://github.com/QUniBone/QUniBone-classic).
Licensed BSD 2-Clause; Jörg's copyright is retained.

## What's new here

- **Web interface** — browser console (xterm.js) with configuration, panel view,
  and log pages, served over HTTP with admin authentication.
- **DELQA Ethernet emulation** — QBUS Ethernet controller bridged onto the host
  LAN, with a station address derived from the board's own MAC.
- **Debian 13 support** — runs on current Debian, loading the PRU firmware
  through `remoteproc`.
- **Distributable SD-card image** — a `.deb` package and image builder that
  self-configure on first boot: SSH host keys, the filesystem grown to fill the
  card, and per-board personalization.
- **Status LEDs** — emulator state on the BeagleBone user LEDs.
- **2.11BSD QBONE kernel configuration** for the emulated machine.

## Building and installing

Cross-build with `crossbuild.sh`. For deploying to a board and building
distributable images, see [`DISTRIBUTION.md`](DISTRIBUTION.md) and
[`debian-installation.md`](debian-installation.md).

## AI-assisted development

This codebase is developed with heavy assistance from AI coding tools, and the
project says so plainly: much of the code here was written, refactored, and
documented with an AI agent in the loop. That is a first-class part of how the
work gets done, not a footnote.

It does not lower the bar. Every change is human-reviewed and tested on real
hardware — a PDP-11 emulation either runs on the bus or it does not. AI is a
force multiplier for a small retrocomputing project; understanding the machine
still rests with the maintainer.

Contributors are welcome to work the same way. If you prefer a hand-written
lineage,
[`QUniBone/QUniBone-classic`](https://github.com/QUniBone/QUniBone-classic) is
Jörg's original.

## License

BSD 2-Clause. © 2019 Jörg Hoppe; © 2026 Hans Hübner and the QUniBone
contributors. See [`LICENSE`](LICENSE).
