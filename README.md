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

## How this code is maintained

The code in this repository is written and maintained by AI agents, including
this README. The maintainer directs and reviews the work; the code itself is
largely agent-authored. This is the ongoing method, not a one-off experiment.

If you would rather not run or contribute to software produced this way, stop
here — Jörg Hoppe's original codebase is preserved at
[`QUniBone/QUniBone-classic`](https://github.com/QUniBone/QUniBone-classic).

## Status

**QBone (QBUS)** runs on real hardware. The DELQA Ethernet emulation passes
DEC's CZQNA diagnostic under XXDP, and 2.11BSD boots and networks over it.

**UniBone (UNIBUS)** is untested in this fork. It cross-compiles, CI builds and
packages it, and the `unibone` image comes off the same script as `qbone` — but
none of it has been exercised on a UniBone in a live backplane. The DEUNA
Ethernet emulation in particular has never been run against an operating system;
it is a port of an unfinished prototype and should be treated as a starting
point for debugging rather than a working device. If you have UNIBUS hardware,
reports are welcome.

## What's new here

- **Web interface** — browser console (xterm.js) with configuration, panel view,
  and log pages, served over HTTP with admin authentication.
- **DELQA Ethernet emulation** — QBUS Ethernet controller bridged onto the host
  LAN, with a station address derived from the board's own MAC.
- **DEUNA Ethernet emulation** — the UNIBUS counterpart. Untested; see Status.
- **Debian 13 support** — runs on current Debian, loading the PRU firmware
  through `remoteproc`.
- **Distributable SD-card image** — a `.deb` package and image builder that
  self-configure on first boot: SSH host keys, the filesystem grown to fill the
  card, and per-board personalization.
- **Status LEDs** — emulator state on the BeagleBone user LEDs.
- **2.11BSD QBONE kernel configuration** for the emulated machine.

## Installing

The supported path is the ready-made card image. It carries Debian 13, the
emulator, the cape overlay and the boot settings, and configures itself on first
boot.

### 1. Get the image

From [Releases](https://github.com/QUniBone/QUniBone/releases), take the newest
`image-*` release and download one of:

- `qbone-dist.img.xz` — QBUS board
- `unibone-dist.img.xz` — UNIBUS board (untested; see Status)

### 2. Write it to a microSD card

8 GB or larger. The image is shrunk to fit the release and grows to fill the
card on first boot.

    xz -dc qbone-dist.img.xz | sudo dd of=/dev/sdX bs=4M status=progress conv=fsync

Replace `/dev/sdX` with the card — on macOS `/dev/rdiskN`, and check it twice,
`dd` will not ask. Raspberry Pi Imager, balenaEtcher and `bmaptool` all handle
the `.xz` directly if you would rather not use `dd`.

### 3. Fit the cape and power on

Fit the cape, insert the card, apply power. The image is built to run from the
microSD; the cape occupies the eMMC data lines, so the eMMC is disabled by the
overlay and unusable while the cape is fitted. If the board comes up on
something other than the card, hold the S2 boot button while applying power to
force SD boot.

### 4. Wait out the first boot

**First boot takes 2–3 minutes and includes a reboot of its own.** Setup runs in
two passes: the first applies boot settings and moves `eth0` onto a bridgeable
driver, then reboots; the second finds everything in place and starts the
emulator. Do not pull power in between — watch the four user LEDs next to the
Ethernet jack instead.

The three LEDs `usr0`–`usr2` show how far the board has got. A growing run of
LEDs blinking together at half-second intervals means it is still working:

| LEDs | Blinking | Meaning |
|---|---|---|
| `X··` | 0.5 s | booting |
| `XX·` | 0.5 s | configuring |
| `XXX` | 0.5 s | starting |
| `X··` `·X·` `··X` `·X·` … | ~150 ms sweep | **ready** |

The bouncing sweep is what you are waiting for: it means the emulator service is
running. Anything else still blinking after about five minutes means setup did
not finish, and the serial console below is how to find out why.

`usr3` is SD-card activity, relocated there because `usr1` is now an indicator.

### 5. Find the board

The board takes a DHCP address on `br0`, the bridge carrying both the BeagleBone
and the emulated machine. Its hostname is `qbone` (or `unibone`). In order of
convenience:

1. **`http://qbone.local/`** — the image runs an mDNS responder, so the name
   resolves without any setup on your network. Your client has to speak mDNS
   too: macOS does, Linux needs `libnss-mdns`, Windows needs Bonjour.
2. **A service browser** — the web interface advertises itself over DNS-SD as
   *QBone on \<hostname\>*, so it appears in Safari's Bonjour bookmarks, in
   `avahi-browse -rt _http._tcp`, and in the network view of most file managers.
3. **Plug a USB cable into the board** — it appears as a network interface with
   the board at a fixed **192.168.7.2**, handing your machine an address on the
   same subnet. No LAN, no DHCP server, nothing to discover:
   `http://192.168.7.2/`.
4. **Your router's DHCP lease table** — look for the hostname. The bridge pins
   the board's uplink MAC, so the lease is stable across reboots.
5. **The serial console** — a 3.3 V USB-serial adapter on the J1 header,
   115200 8N1. The address is printed above the login prompt, so you do not
   need to log in. This is also the console of last resort if the board never
   reaches the network: the USB gadget *serial* getty is masked in the image
   because it wedges the boot, so USB serial is not a way in — only USB
   networking is.

The address is also printed on the console and into the journal once the board
has one (`journalctl -u qbone-announce`).

Then open `http://<address>/`. The web interface binds port 80 and asks you to
set an admin password on first use.

### More than one board on the network

Addresses never collide: each board's DHCP lease is keyed to its own uplink MAC,
and each emulated Ethernet controller derives its station address from that MAC
as well.

Names do. Every image ships the same hostname, so a second board finds `qbone`
taken and mDNS renames it `qbone-2.local`, a third `qbone-3.local`, and so on.
That keeps them reachable but does not tell you which physical board is which —
and which one gets `-2` depends on boot order.

Give each board its own name:

    sudo hostnamectl set-hostname pdp11-front
    sudo sed -i 's/^127\.0\.1\.1.*/127.0.1.1\tpdp11-front/' /etc/hosts
    sudo systemctl restart avahi-daemon systemd-networkd

The name then follows everywhere by itself: `pdp11-front.local`, the DNS-SD
entry, the DHCP lease in your router's table, and the login banner. Nothing in
the emulator depends on the hostname — the `qbone` in paths, unit names and the
package is the board type, not the machine's name, and is unaffected.

## Building from source

Cross-build with `crossbuild.sh` (Docker, no toolchain to install):

    ./crossbuild.sh        # QBUS
    ./crossbuild.sh -u     # UNIBUS

For deploying to a board and building distributable images, see
[`DISTRIBUTION.md`](DISTRIBUTION.md) and
[`debian-installation.md`](debian-installation.md).

## License

BSD 2-Clause. © 2019 Jörg Hoppe; © 2026 Hans Hübner and the QUniBone
contributors. See [`LICENSE`](LICENSE).
