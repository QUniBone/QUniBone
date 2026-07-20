# Distributing QBone as a ready-made card image

A plan for a downloadable image that boots a QBone cape into a running
emulator with its web interface up, on Debian 13, and that stays updateable
afterwards through apt.

What the recipient should have to do: write the image, fit the cape, power on,
open `http://qbone.local/`. What they should be able to do later: `apt upgrade`.

## What already exists

Most of the work is done and scattered across three places.

`packaging/build-deb.sh` produces a complete `qbone_*_armhf.deb`: a static
`/usr/bin/qbone` with the PRU firmware inside it, the frontend under
`/usr/share/qbone/frontend`, both cape overlays in `/lib/firmware`, the
modprobe and modules-load drop-ins, two systemd units, and empty state
directories under `/var/lib/qbone`.

`packaging/debian/qbone-setup` performs the whole board-side procedure:
rewrites `/boot/uEnv.txt`, checks the hardware, bridges the uplink with an
armed rollback, and starts the services.

`debian-installation.md` records the rest - the settings and masks the cape
needs, and the three ways a stock image wedges a board with a cape fitted.

The gap between that and an image is small and specific: the four fixes
`qbone-setup` does not do (`gpio-manager`, the `ttyGS0` getty, nginx on port
80, the persistent journal), a user, sample disk images, and a build that
produces the same artifact twice.

## Decision 1: how the image is produced

**A. Golden master.** Set a board up by hand, `dd` the card back, shrink,
publish. Fastest to a first image and reproducible only by memory. Every
subsequent release repeats the whole manual procedure and quietly inherits
whatever else was on that card - shell history, ssh keys, a half-finished
experiment.

**B. Scripted customisation of the rcn-ee base image.** Take
`am335x-debian-13.6-base-v6.12-armhf-*.img.xz`, loop-mount it, chroot in
under qemu, apply a numbered sequence of steps, shrink, emit. One script, one
input image pinned by sha256, output diffable against the last release. This
is what pi-gen and CustomPiOS do for the Raspberry Pi and it is the right
size for this job.

**C. Build from scratch with rcn-ee's `omap-image-builder`.** Total control,
including the bootloader. It also means owning the U-Boot and kernel packaging
that rcn-ee currently does, which is a standing maintenance cost for no
benefit here - the base image's boot chain is already correct for this board.

**D. A declarative image builder** (debos, mkosi, vmdb2). Cleaner than a
shell script and a real dependency to learn and pin. Worth revisiting if B's
script grows past a few hundred lines.

**Recommendation: B.** It reuses the boot chain that is known to work on this
hardware and keeps the delta from stock visible in one file.

Two practical constraints on where it runs. macOS cannot loop-mount ext4, so
the build belongs in a privileged Linux container - the same Docker the cross
build already uses. And on an Apple Silicon host there is no AArch32
execution, so the armhf chroot runs under `qemu-user-static` binfmt
registration rather than natively.

## Decision 2: where the apt repository lives

The image has to ship with a working `sources.list` entry or the "updateable
with apt" half of this does not exist.

**A. Forgejo's Debian package registry.** `code.netzhansa.com` runs Forgejo
15.0.3 and the registry is live - `/api/packages/hanshuebner/debian/repository.key`
already answers. Upload is a `PUT` to
`/api/packages/hanshuebner/debian/pool/{distribution}/{component}/upload`;
Forgejo signs the release and serves the key. Nothing to host, nothing to
sign by hand, and it sits next to the source.

**B. A static repo built with reprepro or aptly**, published to any web
space. Standard, portable, and it makes signing keys and their rotation your
problem.

**C. Releases only, installed with `dpkg -i`.** No upgrade path.

**Recommendation: A**, with the image shipping

    /etc/apt/keyrings/qbone.gpg
    /etc/apt/sources.list.d/qbone.sources   # signed-by the above

pinned to a distribution name of `trixie` and component `main`. B stays the
fallback if the registry's signing turns out to be awkward to consume.

## Decision 3: the sample operating systems

The disk images left this repository in commit `a11c388` and live at
`files.retrocmp.com/qunibone/`. Runtime expects them in
`/var/lib/qbone/images`, registered through the web interface, each paired
with the `.cmd` script that boots it.

**A. Baked into the image.** Simplest, and it makes every download carry
every sample whether or not it is wanted. It also welds the samples to the
image release cycle.

**B. Separate apt packages** - `qbone-images-rt11`, `qbone-images-xxdp`,
`qbone-images-211bsd`, each dropping into `/var/lib/qbone/images` with a
matching saved configuration. The base image installs one small sample;
anything else is one `apt install`. Samples then version independently of
the emulator, which is what they want to do.

**C. Fetched on demand by the web interface** from a catalogue URL. The best
experience and the most new code - a catalogue format, a downloader, progress
reporting, checksum handling, and a board that needs internet access to be
interesting.

**Recommendation: B, with one sample baked in.** `xxdp25.rl02` is the natural
resident: it is small, it is the diagnostic monitor, and it is the one image
that carries LQA support, so it doubles as the check that a freshly built
board actually works. RT-11, RSX-11M+, Unix V6 and 2.11BSD become their own
packages.

**This needs a licensing answer before anything is published.** 2.11BSD and
Unix V6 are settled - BSD-licensed and covered by the Caldera release
respectively. RT-11, RSX-11M+ and XXDP are DEC software now owned downstream
of HP, distributed in the hobbyist world under a Mentec-era licence whose
current standing nobody has tested. Serving them from a repository under your
own domain is a more exposed position than a user fetching them from
retrocmp. The conservative split: ship only the freely-licensed images in
packages, and have the web interface point at retrocmp for the DEC ones.
Decide this deliberately rather than by default.

## Decision 4: minimal now, full-sized on first boot

The base image already expands its root filesystem on first boot, so the
mechanism exists and the job is to avoid breaking it while making the
published artifact small.

    resize2fs -M                    shrink the filesystem to its contents
    sfdisk                          move the partition end down to match
    truncate                        cut the image file
    xz -9                           compress

A base install plus the emulator and one sample should land near 1.5 GB
uncompressed and a few hundred MB compressed, from a 4 GB starting image.

Verify rather than assume which unit performs the growth on this particular
rcn-ee build, and confirm it still fires after the shrink - a resize service
that keys off a flag file, a partition number, or a size comparison can all
be upset by having the partition table rewritten underneath it. If it is
upset, replacing it with an own `qbone-growfs.service` that runs
`growpart` + `resize2fs` before `multi-user.target` and disables itself is a
dozen lines.

## What the image build applies

In order, in the chroot:

1. **Boot settings.** `disable_uboot_overlay_emmc=1`, `disable_uboot_overlay_video=1`,
   `disable_uboot_overlay_audio=1`, `uboot_overlay_pru` commented out,
   `uboot_overlay_addr4=QBone.dtbo`. `qbone-setup` already writes exactly
   this; the image build can call it or share its code rather than
   duplicating the edits.
2. **Free port 80.** `apt purge nginx nginx-common libnginx-mod-http-fancyindex
   cockpit-ws cockpit-system cockpit-packagekit`, and remove the orphaned
   `/var/www/html/Cockpit.html`. The emulator's web interface binds 80 and
   will not start behind nginx.
3. **`systemctl mask gpio-manager.service`** and `apt-mark hold gpiod`, or
   purge `gpiod` outright - nothing in the image needs it.
4. **`systemctl mask serial-getty@ttyGS0.service`**, which is the failure
   that presents as a broken systemd.
5. **Persistent journal** - `mkdir /var/log/journal`.
6. **The `qbone` user**, in `sudo` and `admin`, with the password expired so
   first login forces a change. Delete the image's default `debian` user.
   NOPASSWD, if wanted, goes in `/etc/sudoers.d/zz-qbone` - the name matters,
   because sudoers is last-match-wins and the image's own `admin` file sorts
   after any numeric prefix.
7. **Install the `qbone` package** and enable both units.
8. **The apt repository** keyring and sources entry.
9. **Identity reset**, last: truncate `/etc/machine-id` to zero bytes, remove
   `/etc/ssh/ssh_host_*`, clear shell history, apt lists and logs, and set
   the hostname to `qbone` so `qbone.local` resolves over mDNS. A published
   image that ships one set of ssh host keys gives every board that identity.
10. **Kernel pin.** `apt-mark hold linux-image-6.12.93-bone63`. 6.12 is the
    newest kernel with an RT build and the one the overlay and pinmux are
    verified against; an unattended upgrade that moves off it changes the
    thing the whole port depends on.

## Package changes, done

- **The units are enabled on install.** A first install enables both without
  starting them, since the emulator needs boot settings `qbone-setup` applies
  and a reboot to pick them up. An upgrade restarts whatever was running and
  leaves a disabled unit disabled.
- **`Depends` names the tools the scripts call** - `iproute2`, `ifupdown`,
  `bridge-utils`. The field is written by `build-deb.sh`, which assembles the
  binary control file field by field; `packaging/debian/control` is not read
  for it, and its `${misc:Depends}` is a debhelper substitution this build
  does not perform.
- **Version 1.6.0-1, distribution `trixie`.**
- **A `postrm` disables the units on removal.**

## The build pipeline

    crossbuild.sh      →  PRU firmware + demo binary
    build-deb.sh       →  qbone_*_armhf.deb
    build-image.sh     →  qbone-<version>-<date>.img.xz + .sha256   (still to write)

`crossbuild.sh` used to fetch the PRU firmware from a live board over scp,
because `clpru` only exists there, which made a release build depend on a
particular BeagleBone being powered on. It now builds the firmware in a
container of its own: TI's code generation tools, pinned to **2.3.1**, the
version that built the firmware the emulator has been verified with. `clpru`
is an x86-64 binary inside an i386 installer, so that container is amd64
whatever the host is and carries a 32-bit C library to unpack it. The
`prussdrv` headers, formerly scp'd from the board as well, are in the tree.

A build therefore needs this repository and Docker, and nothing else.
`.github/workflows/build.yml` runs it on every push: cross-compile both
platforms, rebuild the firmware from clean and require the two builds to
agree, package, and check the maintainer scripts parse. Publishing runs on
tags only and skips itself unless a registry token is configured.

**The firmware this produces is not bit-identical to what the board built.**
Same compiler version, three extra instructions, all in the C runtime startup
region - the board's `clpru` came from the BeagleBoard Debian package and TI's
own installer evidently ships a different `libc.a`. Two container builds agree
with each other, so the build is reproducible; what is unverified is the new
firmware against real hardware. **Run the diagnostics on a live machine before
any of this is released.**

Both platform builds emit a file called `qbone_<version>_armhf.deb`. Only the
QBUS one is published, because a UNIBUS package needs a package name of its
own before the two can share a repository.

## Web interface authentication, done

A board with no password answers every request, which is what makes a fresh
one reachable. The frontend now asks for an admin password the first time it
reaches a board that has none, in a dialog with no cancel, and PUTs it to
`/api/auth`; from then on every request needs it, static files and WebSocket
handshakes included.

The password is kept in `settings.json` as a PBKDF2-HMAC-SHA256 digest over a
random salt, at 120000 iterations. The build links no crypto library - it is
static and civetweb is compiled `-DNO_SSL` - so SHA-256, HMAC and PBKDF2 are
in `webauth.cpp`, checked against the FIPS 180-4, RFC 4231 and RFC 7914
vectors. `settings.json` now carries a credential, so it is written through a
private temporary and renamed, mode 0600.

Basic auth resends the password on every request and PBKDF2 costs more than
serving the page, so a password that verifies once is remembered as a single
SHA-256 over a salt generated afresh at each start. One request per run of
the process pays the full derivation.

`WEBUI_PASSWORD` still works and outranks the file; a board set up that way
reports its password as coming from the environment and refuses to change it
through the interface.

**The window is narrower, not closed.** Between first boot and someone
setting a password, the board is open to anyone who can reach it - the same
exposure as before, now bounded by how quickly the first page is opened. An
image that generated a password at first boot and printed it on the serial
console would close it completely.

## Open questions

- **Does the firmware built here behave on real hardware?** It differs from
  the board's own build in the C runtime startup. Nothing else in this plan
  matters until that is answered on a live machine.
- Does the base image's rootfs growth survive the shrink, and which unit
  performs it?
- RT or non-RT kernel in the shipped image? The port runs on the non-RT
  `6.12.93-bone63` against a live 11/73 and whether RT is needed at all is
  still unmeasured. Shipping non-RT keeps the measurement honest.
- Does the image ship with the uplink already bridged, or does it leave that
  to a first `qbone-setup` run? Bridging at build time cannot arm the
  rollback that makes it safe, which argues for leaving it.
- The apt repository host. CI is GitHub Actions, matching this repository's
  `origin`; the Debian registry on `code.netzhansa.com` is verified working
  and is where the publish step points, which splits the two across hosts.
