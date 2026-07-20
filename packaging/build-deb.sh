#!/bin/bash
# Build the qbone Debian package from an already cross-compiled binary.
#
# The executable is called "demo" in the source tree and "qbone" once
# installed; the rename happens here so the tree stays mergeable with
# upstream.
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

BINARY=10.03_app_demo/4_deploy$SUFFIX/demo
if [ ! -x $BINARY ]; then
    echo "no binary at $BINARY - run ./crossbuild.sh first" >&2
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
    $STAGE/usr/share/doc/qbone/examples \
    $STAGE/var/lib/qbone/images \
    $STAGE/var/lib/qbone/configs

install -m 755 $BINARY $STAGE/usr/bin/qbone
install -m 644 10.05_web/3_frontend/index.html $STAGE/usr/share/qbone/frontend/
install -m 644 10.05_web/3_frontend/vendor/* $STAGE/usr/share/qbone/frontend/vendor/
install -m 644 packaging/debian/qbone.service $STAGE/lib/systemd/system/
install -m 644 packaging/debian/startup.cmd $STAGE/etc/qbone/
install -m 644 packaging/debian/network.conf $STAGE/etc/qbone/
install -m 755 packaging/debian/qbone-network $STAGE/usr/sbin/
install -m 755 packaging/debian/qbone-setup $STAGE/usr/sbin/
install -m 644 packaging/debian/qbone-network.service $STAGE/lib/systemd/system/
install -m 644 packaging/debian/examples/interfaces-bridge $STAGE/usr/share/doc/qbone/examples/
install -m 644 packaging/debian/README.Debian $STAGE/usr/share/doc/qbone/

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
    # The emulator itself is static and needs nothing. These are for the two
    # scripts: qbone-network builds the veth pair with ip(8), and qbone-setup
    # writes an interfaces.d stanza and brings the bridge up with ifup(8).
    # Spelled out here rather than taken from packaging/debian/control, whose
    # ${misc:Depends} is a debhelper substitution this build does not perform.
    echo "Depends: iproute2, ifupdown, bridge-utils"
    echo "Maintainer: Hans Huebner <hans.huebner@gmail.com>"
    echo "Installed-Size: $INSTALLED_KB"
    sed -n '/^Description:/,$p' packaging/debian/control
} > $STAGE/DEBIAN/control

# files under /etc, which dpkg must not overwrite once they have been edited
{
    echo "/etc/qbone/startup.cmd"
    echo "/etc/qbone/network.conf"
    echo "/etc/modprobe.d/qbone.conf"
    echo "/etc/modules-load.d/qbone.conf"
} > $STAGE/DEBIAN/conffiles

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
chmod 755 $STAGE/DEBIAN/postinst $STAGE/DEBIAN/prerm $STAGE/DEBIAN/postrm

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
