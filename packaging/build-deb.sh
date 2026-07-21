#!/bin/bash
# Build the qbone Debian package from an already cross-compiled binary.
#
# Two programs are installed. "qbone-web" in the source tree becomes
# /usr/bin/qbone, the service the unit runs: it serves the web interface and
# has no menu. "demo" becomes /usr/bin/qbone-demo, the interactive tool for
# bus latches, master/slave tests and the device exercisers. The renames
# happen here so the tree stays mergeable with upstream.
#
# The binary is statically linked and carries the PRU firmware inside it, so
# the package depends on nothing and the staging tree is small. dpkg-deb runs
# in the same container the cross build uses, since macOS has no dpkg.
#
# usage:
#   ./packaging/build-deb.sh            package the QBUS build
#   ./packaging/build-deb.sh -u         package the UNIBUS build

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

BINARY=10.03_app_demo/4_deploy$SUFFIX/qbone-web
BINARY_DEMO=10.03_app_demo/4_deploy$SUFFIX/demo
if [ ! -x $BINARY ] || [ ! -x $BINARY_DEMO ]; then
    echo "no binary at $BINARY / $BINARY_DEMO - run ./crossbuild.sh first" >&2
    exit 1
fi

VERSION=$(sed -n 's/^qbone (\([^)]*\)).*/\1/p' packaging/debian/changelog | head -1)
STAGE=$(mktemp -d)
trap 'rm -rf "$STAGE"' EXIT
# mktemp makes it private; the package root has to be world readable
chmod 755 "$STAGE"

install -d -m 755 $STAGE/DEBIAN \
    $STAGE/etc/modprobe.d \
    $STAGE/etc/modules-load.d \
    $STAGE/usr/bin \
    $STAGE/usr/share/qbone/frontend/vendor \
    $STAGE/usr/share/doc/qbone \
    $STAGE/etc/qbone \
    $STAGE/lib/systemd/system \
    $STAGE/lib/firmware \
    $STAGE/usr/sbin \
    $STAGE/usr/share/qbone/network \
    $STAGE/var/lib/qbone/images \
    $STAGE/var/lib/qbone/configs

install -m 755 $BINARY $STAGE/usr/bin/qbone
install -m 755 $BINARY_DEMO $STAGE/usr/bin/qbone-demo
install -m 644 10.05_web/3_frontend/index.html $STAGE/usr/share/qbone/frontend/
install -m 644 10.05_web/3_frontend/vendor/* $STAGE/usr/share/qbone/frontend/vendor/
# favicons and the PWA manifest, served from the web root beside index.html
install -m 644 10.05_web/3_frontend/favicon.ico 10.05_web/3_frontend/favicon.svg \
    10.05_web/3_frontend/favicon-16x16.png 10.05_web/3_frontend/favicon-32x32.png \
    10.05_web/3_frontend/favicon-48x48.png 10.05_web/3_frontend/apple-touch-icon.png \
    10.05_web/3_frontend/android-chrome-192x192.png \
    10.05_web/3_frontend/android-chrome-512x512.png \
    10.05_web/3_frontend/site.webmanifest $STAGE/usr/share/qbone/frontend/
install -m 644 packaging/debian/qbone.service $STAGE/lib/systemd/system/
install -m 644 packaging/debian/network.conf $STAGE/etc/qbone/
install -m 755 packaging/debian/qbone-network $STAGE/usr/sbin/
install -m 755 packaging/debian/qbone-setup $STAGE/usr/sbin/
install -m 644 packaging/debian/qbone-network.service $STAGE/lib/systemd/system/
# runs qbone-setup --auto unattended; enabled on the distribution image, left
# disabled on a package install where the operator drives qbone-setup by hand
install -m 644 packaging/debian/qbone-setup.service $STAGE/lib/systemd/system/
install -m 644 packaging/debian/README.Debian $STAGE/usr/share/doc/qbone/
# qbone-setup builds this into the loaded DTB so eth0 is a plain, bridgeable NIC
install -m 644 02_bbb_config/01_cape/am335x-boneblack-qbone.dts $STAGE/usr/share/qbone/
# the bridge that carries the emulated machine, installed by qbone-setup
install -m 644 packaging/debian/network/br0.netdev packaging/debian/network/br0.network \
    packaging/debian/network/eth0.network packaging/debian/network/veth-br.network \
    packaging/debian/network/veth-pdp.network $STAGE/usr/share/qbone/network/

# Both cape overlays: capemgr loads UniBone-00B0.dtbo from the cape's EEPROM
# up to 4.19, and U-Boot applies QBone.dtbo by name after it.
install -m 644 02_bbb_config/01_cape/UniBone-00B0.dtbo $STAGE/lib/firmware/
dtc -@ -I dts -O dtb -o $STAGE/lib/firmware/QBone.dtbo \
    02_bbb_config/01_cape/QBone.dtso 2>&1 \
    | grep -v "ranges_format\|avoid_default_addr_size\|avoid_unnecessary_addr_size\|unique_unit_address" || true
[ -s $STAGE/lib/firmware/QBone.dtbo ] || { echo "dtc produced no overlay" >&2; exit 1; }
chmod 644 $STAGE/lib/firmware/QBone.dtbo

# uio_pdrv_genirq matches no compatible of its own; the one it looks for is a
# module parameter, so the overlay's node binds to nothing until it is set.
install -m 644 packaging/debian/modprobe-qbone.conf $STAGE/etc/modprobe.d/qbone.conf
install -m 644 packaging/debian/modules-load-qbone.conf $STAGE/etc/modules-load.d/qbone.conf
gzip -9 -n -c packaging/debian/changelog > $STAGE/usr/share/doc/qbone/changelog.Debian.gz
chmod 644 $STAGE/usr/share/doc/qbone/changelog.Debian.gz

# binary control file: the source stanza's fields, plus the installed size
INSTALLED_KB=$(du -sk $STAGE | cut -f1)
{
    echo "Package: qbone"
    echo "Version: $VERSION"
    echo "Section: misc"
    echo "Priority: optional"
    echo "Architecture: armhf"
    # The emulator itself is static and needs nothing. iproute2 is for the
    # ip(8) calls in qbone-network and qbone-setup; device-tree-compiler, cpp
    # and make let qbone-setup build the legacy-Ethernet device tree. The
    # operator toolset and the nginx removal belong to the image preparation,
    # not here. Spelled out rather than taken from packaging/debian/control,
    # whose ${misc:Depends} is a debhelper substitution this build does not do.
    echo "Depends: iproute2, device-tree-compiler, cpp, make"
    echo "Maintainer: Hans Huebner <hans.huebner@gmail.com>"
    echo "Installed-Size: $INSTALLED_KB"
    sed -n '/^Description:/,$p' packaging/debian/control
} > $STAGE/DEBIAN/control

# files under /etc, which dpkg must not overwrite once they have been edited
{
    echo "/etc/qbone/network.conf"
    echo "/etc/modprobe.d/qbone.conf"
    echo "/etc/modules-load.d/qbone.conf"
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
chmod 755 $STAGE/DEBIAN/preinst $STAGE/DEBIAN/postinst $STAGE/DEBIAN/prerm $STAGE/DEBIAN/postrm

# md5sums over everything outside DEBIAN
( cd $STAGE && find . -type f ! -path "./DEBIAN/*" -printf "%P\0" \
    | xargs -0 md5sum > DEBIAN/md5sums )
chmod 644 $STAGE/DEBIAN/md5sums

# xz keeps the archive readable by the dpkg on Debian 8, which predates zstd
OUT=qbone_${VERSION}_armhf.deb
dpkg-deb -Zxz --build --root-owner-group "$STAGE" "$OUT"
echo
dpkg-deb --info "$OUT"
echo
dpkg-deb --contents "$OUT"
