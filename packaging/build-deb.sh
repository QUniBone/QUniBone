#!/bin/bash
# Build the Debian package from an already cross-compiled binary. The QBUS build
# ships as "qbone", the UNIBUS build as "unibone" - same packaging, one brand
# rewritten to the other.
#
# Two programs are installed. "qbone-web" in the source tree becomes
# /usr/bin/<name>, the service the unit runs: it serves the web interface and
# has no menu. "demo" becomes /usr/bin/<name>-demo, the interactive tool for
# bus latches, master/slave tests and the device exercisers. The renames
# happen here so the tree stays mergeable with upstream.
#
# The binary is statically linked and carries the PRU firmware inside it, so
# the package depends on nothing and the staging tree is small. dpkg-deb runs
# in the same container the cross build uses, since macOS has no dpkg.
#
# usage:
#   ./packaging/build-deb.sh            package the QBUS build as "qbone"
#   ./packaging/build-deb.sh -u         package the UNIBUS build as "unibone"

set -e
cd "$(dirname "$0")/.."

# dpkg-deb, GNU find and dtc are all Debian tools, and the host is usually
# macOS, so the packaging runs in a container. Re-enter one when the tools are
# not here; inside, the check passes and the script continues.
IMAGE=qunibone-package
if ! command -v dpkg-deb >/dev/null 2>&1 || ! command -v dtc >/dev/null 2>&1; then
    if ! command -v docker >/dev/null 2>&1; then
        echo "needs dpkg-deb and dtc, or docker to supply them" >&2
        exit 1
    fi
    if ! docker image inspect $IMAGE >/dev/null 2>&1; then
        echo "Building $IMAGE docker image ..."
        docker build -t $IMAGE - <<'EOF'
FROM debian:bookworm-slim
RUN apt-get update && apt-get install -y --no-install-recommends \
        device-tree-compiler xz-utils \
        gcc-arm-linux-gnueabihf libc6-dev-armhf-cross \
    && rm -rf /var/lib/apt/lists/*
EOF
    fi
    exec docker run --rm -v "$PWD:/qunibone" -w /qunibone $IMAGE \
        ./packaging/build-deb.sh "$@"
fi

SUFFIX=_q
while getopts "u" opt; do
    case $opt in
        u) SUFFIX=_u;;
        *) exit 1;;
    esac
done

# Board identity. The lowercase brand names the package, its install paths and
# its systemd units; the display brand is what the web interface shows. QBUS is
# qbone/QBone, UNIBUS is unibone/UniBone.
if [ "$SUFFIX" = _u ]; then
    NAME=unibone; DISPLAY=UniBone; OTHER=qbone
else
    NAME=qbone;   DISPLAY=QBone;   OTHER=unibone
fi

# Rewrite the qbone/QBone brand to this board's, while keeping the hardware
# tokens both boards share through the bus-agnostic QBone cape overlay: the
# qbone-ddr reserved-memory node, the QBone.dtbo overlay filename, and the
# "qbone" UIO device name. For the qbone board every substitution is identity,
# so its package is byte-for-byte what it was before this was parametrized.
rebrand() {
    sed -e "s/qbone/$NAME/g" \
        -e "s/QBone/$DISPLAY/g" \
        -e "s/${NAME}-ddr/qbone-ddr/g" \
        -e "s/${DISPLAY}\\.dtbo/QBone.dtbo/g" \
        -e "s/grep -qi ${NAME}/grep -qi qbone/g"
}

BINARY=10.03_app_demo/4_deploy$SUFFIX/qbone-web
BINARY_DEMO=10.03_app_demo/4_deploy$SUFFIX/demo
if [ ! -x $BINARY ] || [ ! -x $BINARY_DEMO ]; then
    echo "no binary at $BINARY / $BINARY_DEMO - run ./crossbuild.sh first" >&2
    exit 1
fi

VERSION=$(sed -n 's/^[a-z]* (\([^)]*\)).*/\1/p' packaging/debian/changelog | head -1)
STAGE=$(mktemp -d)
trap 'rm -rf "$STAGE"' EXIT
# mktemp makes it private; the package root has to be world readable
chmod 755 "$STAGE"

install -d -m 755 $STAGE/DEBIAN \
    $STAGE/etc/modprobe.d \
    $STAGE/etc/modules-load.d \
    $STAGE/usr/bin \
    $STAGE/usr/share/$NAME/frontend/vendor \
    $STAGE/usr/share/doc/$NAME \
    $STAGE/etc/$NAME \
    $STAGE/lib/systemd/system \
    $STAGE/lib/firmware \
    $STAGE/usr/sbin \
    $STAGE/usr/share/$NAME/network \
    $STAGE/var/lib/$NAME/images \
    $STAGE/var/lib/$NAME/configs

install -m 755 $BINARY $STAGE/usr/bin/$NAME
install -m 755 $BINARY_DEMO $STAGE/usr/bin/$NAME-demo
# the web root: index.html and the manifest carry the display brand
rebrand < 10.05_web/3_frontend/index.html > $STAGE/usr/share/$NAME/frontend/index.html
rebrand < 10.05_web/3_frontend/site.webmanifest > $STAGE/usr/share/$NAME/frontend/site.webmanifest
chmod 644 $STAGE/usr/share/$NAME/frontend/index.html \
    $STAGE/usr/share/$NAME/frontend/site.webmanifest
install -m 644 10.05_web/3_frontend/vendor/* $STAGE/usr/share/$NAME/frontend/vendor/
# favicons served from the web root beside index.html (binary, copied as-is)
install -m 644 10.05_web/3_frontend/favicon.ico 10.05_web/3_frontend/favicon.svg \
    10.05_web/3_frontend/favicon-16x16.png 10.05_web/3_frontend/favicon-32x32.png \
    10.05_web/3_frontend/favicon-48x48.png 10.05_web/3_frontend/apple-touch-icon.png \
    10.05_web/3_frontend/android-chrome-192x192.png \
    10.05_web/3_frontend/android-chrome-512x512.png \
    $STAGE/usr/share/$NAME/frontend/
# systemd units, helper scripts and configs: brand rewritten, renamed to $NAME
rebrand < packaging/debian/qbone.service > $STAGE/lib/systemd/system/$NAME.service
rebrand < packaging/debian/network.conf > $STAGE/etc/$NAME/network.conf
rebrand < packaging/debian/qbone-network > $STAGE/usr/sbin/$NAME-network
rebrand < packaging/debian/qbone-setup > $STAGE/usr/sbin/$NAME-setup
rebrand < packaging/debian/qbone-resize > $STAGE/usr/sbin/$NAME-resize
rebrand < packaging/debian/qbone-announce > $STAGE/usr/sbin/$NAME-announce
rebrand < packaging/debian/qbone-rename > $STAGE/usr/sbin/$NAME-rename
chmod 755 $STAGE/usr/sbin/$NAME-network $STAGE/usr/sbin/$NAME-setup \
    $STAGE/usr/sbin/$NAME-resize $STAGE/usr/sbin/$NAME-announce \
    $STAGE/usr/sbin/$NAME-rename
# Board-neutral aliases for the administrative tools. Both boards are a
# BeagleBone whatever the bus, so one set of commands serves both and the
# documentation can name them without picking a side. The branded names stay,
# for anyone who thinks in their own board's terms. Only one board's package
# installs at a time - they conflict - so these never contend.
for verb in setup rename network resize announce; do
    ln -sf $NAME-$verb $STAGE/usr/sbin/bone-$verb
done
rebrand < packaging/debian/qbone-network.service > $STAGE/lib/systemd/system/$NAME-network.service
# runs <name>-setup --auto unattended; enabled on the distribution image, left
# disabled on a package install where the operator drives the setup by hand
rebrand < packaging/debian/qbone-setup.service > $STAGE/lib/systemd/system/$NAME-setup.service
# status LEDs: a tiny standalone daemon, cross-compiled here. Enabled on the
# image; shipped disabled in the package. Rebrand its unit-name/path literals
# before compiling, since they are baked into the binary.
LEDS_C=$(mktemp --suffix=.c)
rebrand < packaging/debian/qbone-leds.c > "$LEDS_C"
arm-linux-gnueabihf-gcc -O2 -Wall -o $STAGE/usr/sbin/$NAME-leds "$LEDS_C"
rm -f "$LEDS_C"
rebrand < packaging/debian/qbone-leds.service > $STAGE/lib/systemd/system/$NAME-leds.service
# grows the root filesystem to fill the card on first boot; enabled on the image
rebrand < packaging/debian/qbone-resize.service > $STAGE/lib/systemd/system/$NAME-resize.service
# prints the board's address on the console once the bridge has one; enabled on
# the image, where nobody knows the address yet
rebrand < packaging/debian/qbone-announce.service > $STAGE/lib/systemd/system/$NAME-announce.service
# DNS-SD advertisement for the web interface. A template: <name>-setup
# substitutes the board's identifier and installs it under /etc/avahi/services,
# so the file under /etc is generated rather than a conffile every board would
# show as locally modified.
rebrand < packaging/debian/avahi-qbone.service > $STAGE/usr/share/$NAME/avahi-$NAME.service
chmod 644 $STAGE/usr/share/$NAME/avahi-$NAME.service
rebrand < packaging/debian/README.Debian > $STAGE/usr/share/doc/$NAME/README.Debian
chmod 644 $STAGE/lib/systemd/system/$NAME*.service $STAGE/etc/$NAME/network.conf \
    $STAGE/usr/share/doc/$NAME/README.Debian
# <name>-setup builds this into the loaded DTB so eth0 is a plain, bridgeable
# NIC. The DTS content is the same eth0 fix for both boards; only its name
# tracks the brand.
install -m 644 02_bbb_config/01_cape/am335x-boneblack-qbone.dts \
    $STAGE/usr/share/$NAME/am335x-boneblack-$NAME.dts
# the bridge that carries the emulated machine, installed by <name>-setup
for f in br0.netdev br0.network eth0.network veth-br.network veth-pdp.network usb0.network; do
    rebrand < packaging/debian/network/$f > $STAGE/usr/share/$NAME/network/$f
    chmod 644 $STAGE/usr/share/$NAME/network/$f
done

# Both cape overlays: capemgr loads UniBone-00B0.dtbo from the cape's EEPROM
# up to 4.19, and U-Boot applies QBone.dtbo by name after it. Both boards use
# the same bus-agnostic overlay, so these names do not track the brand.
install -m 644 02_bbb_config/01_cape/UniBone-00B0.dtbo $STAGE/lib/firmware/
dtc -@ -I dts -O dtb -o $STAGE/lib/firmware/QBone.dtbo \
    02_bbb_config/01_cape/QBone.dtso 2>&1 \
    | grep -v "ranges_format\|avoid_default_addr_size\|avoid_unnecessary_addr_size\|unique_unit_address" || true
[ -s $STAGE/lib/firmware/QBone.dtbo ] || { echo "dtc produced no overlay" >&2; exit 1; }
chmod 644 $STAGE/lib/firmware/QBone.dtbo

# uio_pdrv_genirq matches no compatible of its own; the one it looks for is a
# module parameter, so the overlay's node binds to nothing until it is set.
rebrand < packaging/debian/modprobe-qbone.conf > $STAGE/etc/modprobe.d/$NAME.conf
rebrand < packaging/debian/modules-load-qbone.conf > $STAGE/etc/modules-load.d/$NAME.conf
chmod 644 $STAGE/etc/modprobe.d/$NAME.conf $STAGE/etc/modules-load.d/$NAME.conf
rebrand < packaging/debian/changelog | gzip -9 -n -c > $STAGE/usr/share/doc/$NAME/changelog.Debian.gz
chmod 644 $STAGE/usr/share/doc/$NAME/changelog.Debian.gz

# binary control file: the source stanza's fields, plus the installed size
INSTALLED_KB=$(du -sk $STAGE | cut -f1)
{
    echo "Package: $NAME"
    echo "Version: $VERSION"
    echo "Section: misc"
    echo "Priority: optional"
    echo "Architecture: armhf"
    # The emulator itself is static and needs nothing. iproute2 is for the
    # ip(8) calls in <name>-network and <name>-setup; device-tree-compiler, cpp
    # and make let <name>-setup build the legacy-Ethernet device tree. The
    # operator toolset and the nginx removal belong to the image preparation,
    # not here. Spelled out rather than taken from packaging/debian/control,
    # whose ${misc:Depends} is a debhelper substitution this build does not do.
    echo "Depends: iproute2, device-tree-compiler, cpp, make"
    # the two boards ship the same cape overlay and firmware files, and a BBB
    # carries one cape, so they are mutually exclusive on a machine
    echo "Conflicts: $OTHER"
    echo "Replaces: $OTHER"
    echo "Maintainer: Hans Huebner <hans.huebner@gmail.com>"
    echo "Installed-Size: $INSTALLED_KB"
    sed -n '/^Description:/,$p' packaging/debian/control | rebrand
} > $STAGE/DEBIAN/control

# files under /etc, which dpkg must not overwrite once they have been edited
{
    echo "/etc/$NAME/network.conf"
    echo "/etc/modprobe.d/$NAME.conf"
    echo "/etc/modules-load.d/$NAME.conf"
} > $STAGE/DEBIAN/conffiles

cat > $STAGE/DEBIAN/preinst <<'PREINST'
#!/bin/sh
set -e
# The emulator took its startup commands from this file while it was the menu
# program. The service has no menu, and the machine a board comes up as is a
# saved configuration, so the file is obsolete: dpkg leaves a conffile behind
# when a package stops shipping it, and this removes it.
if [ -e /etc/qbone/startup.cmd ]; then
    # The helper also clears dpkg's own record of the conffile, but it acts
    # only when the version being replaced is older than its guard. Reinstalls
    # and rebuilds of one version leave the file behind, so remove it either
    # way: nothing reads it any more.
    if [ -x /usr/bin/dpkg-maintscript-helper ] \
            && dpkg-maintscript-helper supports rm_conffile 2>/dev/null; then
        dpkg-maintscript-helper rm_conffile /etc/qbone/startup.cmd 1.6.0-1~ -- "$@" || true
    fi
    rm -f /etc/qbone/startup.cmd
fi
PREINST
cat > $STAGE/DEBIAN/postinst <<'POSTINST'
#!/bin/sh
set -e
if [ "$1" = configure ]; then
    if [ -d /run/systemd/system ]; then
        systemctl daemon-reload || true
        # $2 is the previously configured version, empty on a first install.
        # Enabling only then keeps a unit the operator disabled disabled.
        # The emulator needs boot settings qbone-setup applies and a reboot to
        # pick them up, so a first install enables without starting.
        if [ -z "$2" ]; then
            systemctl enable qbone-network.service qbone.service || true
        else
            for unit in qbone-network.service qbone.service; do
                if systemctl is-active --quiet $unit; then
                    systemctl restart $unit || true
                fi
            done
        fi
    fi
    echo "qbone: run 'sudo qbone-setup' to configure the boot settings, the"
    echo "qbone: network and the services. See /usr/share/doc/qbone/README.Debian"
fi
POSTINST
cat > $STAGE/DEBIAN/prerm <<'PRERM'
#!/bin/sh
set -e
if [ "$1" = remove ] && [ -d /run/systemd/system ]; then
    systemctl stop qbone.service || true
    systemctl stop qbone-network.service || true
fi
PRERM
cat > $STAGE/DEBIAN/postrm <<'POSTRM'
#!/bin/sh
set -e
if [ "$1" = remove ] && [ -d /run/systemd/system ]; then
    systemctl disable qbone.service qbone-network.service || true
    systemctl daemon-reload || true
fi
POSTRM
# the maintainer scripts are written with the qbone brand, then rewritten in
# place to this board's, so $NAME does not have to be threaded through their
# runtime-shell variables ($1, $2, $@)
for s in preinst postinst prerm postrm; do
    rebrand < "$STAGE/DEBIAN/$s" > "$STAGE/DEBIAN/$s.rebranded"
    mv "$STAGE/DEBIAN/$s.rebranded" "$STAGE/DEBIAN/$s"
done
chmod 755 $STAGE/DEBIAN/preinst $STAGE/DEBIAN/postinst $STAGE/DEBIAN/prerm $STAGE/DEBIAN/postrm

# md5sums over everything outside DEBIAN
( cd $STAGE && find . -type f ! -path "./DEBIAN/*" -printf "%P\0" \
    | xargs -0 md5sum > DEBIAN/md5sums )
chmod 644 $STAGE/DEBIAN/md5sums

# xz keeps the archive readable by the dpkg on Debian 8, which predates zstd
OUT=${NAME}_${VERSION}_armhf.deb
dpkg-deb -Zxz --build --root-owner-group "$STAGE" "$OUT"
echo
dpkg-deb --info "$OUT"
echo
dpkg-deb --contents "$OUT"
