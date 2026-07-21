#!/bin/bash
# build-image.sh - build the QBone distribution image from the rcn-ee base.
#
# Produces a bootable microSD image: the rcn-ee Debian base, customised into a
# QBone appliance - the emulator installed, eth0 moved to the legacy Ethernet
# driver, boot settings applied, the operator toolset added, nginx/cockpit
# removed, and the sample operating systems and their boot configurations
# placed under /var/lib/qbone.
#
# The Linux-specific work (loop-mounting ext4, an armhf chroot, resizing) runs
# in a privileged Docker container, because macOS can do none of it. Apple
# Silicon runs the armhf chroot under qemu-user-static.
#
# Inputs, under $DIST (default ./dist):
#   base.img.xz          the rcn-ee base-v6.12 armhf image
#   qbone_*_armhf.deb    the qbone package
#   images/              disk images to ship (*.dsk, *.rl02, ...)
#   configs/             boot configurations (*.json) naming those images
# and from the repository:
#   02_bbb_config/01_cape/am335x-boneblack-qbone.dts
#   packaging/debian/network/*
#
# Output: $OUT (default ./qbone-dist.img), ready to write to a card.
#
# usage:  DIST=./dist OUT=./qbone-dist.img ./packaging/build-image.sh

set -euo pipefail

HERE=$(cd "$(dirname "$0")/.." && pwd)
DIST=${DIST:-$HERE/dist}
OUT=${OUT:-$HERE/qbone-dist.img}
# room to add beyond the base while building; the image is shrunk to fit after
GROW=${GROW:-4G}
# margin left in the root filesystem after the shrink
MARGIN_MB=${MARGIN_MB:-400}

ls "$DIST"/base.img.xz >/dev/null 2>&1 || { echo "missing $DIST/base.img.xz" >&2; exit 1; }
ls "$DIST"/qbone_*_armhf.deb >/dev/null 2>&1 || { echo "missing $DIST/qbone_*_armhf.deb" >&2; exit 1; }
[ "$(ls "$DIST"/qbone_*_armhf.deb | wc -l)" -eq 1 ] || { echo "expected exactly one qbone deb in $DIST" >&2; exit 1; }
[ -d "$DIST/images" ] || { echo "missing $DIST/images" >&2; exit 1; }
[ -d "$DIST/configs" ] || { echo "missing $DIST/configs" >&2; exit 1; }
DTS=$HERE/02_bbb_config/01_cape/am335x-boneblack-qbone.dts
NET=$HERE/packaging/debian/network
[ -r "$DTS" ] || { echo "missing $DTS" >&2; exit 1; }

echo "== registering the armhf binfmt handler in the Docker VM =="
docker run --privileged --rm tonistiigi/binfmt --install arm >/dev/null

echo "== building the image in a privileged container (this takes a while) =="
docker run --rm -i --privileged \
    -e GROW="$GROW" -e MARGIN_MB="$MARGIN_MB" \
    -v "$DIST":/dist \
    -v "$DTS":/in/qbone.dts:ro \
    -v "$NET":/in/network:ro \
    debian:trixie bash -euo pipefail -s <<'CONTAINER'
export DEBIAN_FRONTEND=noninteractive
apt-get -qq update >/dev/null
apt-get -qq install -y xz-utils fdisk util-linux e2fsprogs dosfstools \
    qemu-user-static systemd >/dev/null

echo "-- decompressing the base image"
xz -dc /dist/base.img.xz > /work.img

echo "-- growing it by $GROW to make room while building"
truncate -s +"$GROW" /work.img
# extend the third partition (root) to the new end of the disk
echo ", +" | sfdisk -q -N 3 /work.img >/dev/null

LO=$(losetup -Pf --show /work.img)
trap 'umount -R /mnt 2>/dev/null || true; losetup -d "$LO" 2>/dev/null || true' EXIT
# the container has no udev, so the kernel's partition block devices have no
# /dev nodes; create them from what the kernel published under /sys
LB=$(basename "$LO")
partx -a "$LO" 2>/dev/null || true
for sp in /sys/block/"$LB"/"$LB"p*; do
    [ -e "$sp/dev" ] || continue
    name=$(basename "$sp"); IFS=: read -r maj min < "$sp/dev"
    [ -e "/dev/$name" ] || mknod "/dev/$name" b "$maj" "$min"
done
e2fsck -pf "${LO}p3" >/dev/null 2>&1 || true
resize2fs "${LO}p3" >/dev/null

mkdir -p /mnt
mount "${LO}p3" /mnt
# /proc and /dev are enough for apt and the device-tree build; /sys is left out
# so the target's qbone-setup logic never sees the container's eth0
mount -t proc proc /mnt/proc
mount --bind /dev /mnt/dev
# resolv.conf is a symlink into /run on the base image; give the chroot a real
# file so apt can resolve, and remember the link to restore afterwards
RESOLV_LINK=$(readlink /mnt/etc/resolv.conf 2>/dev/null || true)
cp --remove-destination /etc/resolv.conf /mnt/etc/resolv.conf

# stage the inputs inside the rootfs
mkdir -p /mnt/tmp/in/images /mnt/tmp/in/configs
cp /dist/qbone_*_armhf.deb /mnt/tmp/in/
cp /dist/images/* /mnt/tmp/in/images/
cp /dist/configs/* /mnt/tmp/in/configs/
cp /in/qbone.dts /mnt/tmp/in/qbone.dts

echo "-- customising the rootfs (armhf chroot)"
chroot /mnt /bin/bash -euo pipefail <<'CHROOT'
export DEBIAN_FRONTEND=noninteractive

# free port 80: the emulator's web interface binds it and will not start behind
# nginx, which fronts cockpit on the base image
apt-get -qq purge -y nginx nginx-common libnginx-mod-http-fancyindex \
    cockpit-ws cockpit-system cockpit-packagekit 2>/dev/null || true
rm -f /var/www/html/Cockpit.html

apt-get -qq update >/dev/null
# the operator toolset the appliance is run and debugged with
apt-get -qq install -y gdb tcpdump zsh tmux ckermit >/dev/null
# the emulator package pulls in iproute2 and the device-tree build tools
apt-get -qq install -y /tmp/in/qbone_*_armhf.deb >/dev/null

# nothing in the image uses the GPIO daemon
apt-get -qq purge -y gpiod 2>/dev/null || true

# keep the kernel the cape overlay and pinmux are verified against; an
# unattended upgrade off it changes what the port depends on
KIMG=$(dpkg-query -W -f='${Package}\n' 'linux-image-*' 2>/dev/null | grep bone | head -1)
[ -n "$KIMG" ] && apt-mark hold "$KIMG" >/dev/null 2>&1 || true

# Passwordless root at the physical console. It is reachable over ssh neither
# with that empty password nor at all: the drop-in denies empty-password
# logins and root logins. sshd_config includes this directory before its own
# body and takes the first value for each keyword, so these win. Ordinary
# password logins (the base image's debian account) still work, for onboarding.
passwd -d root
install -d -m 755 /etc/ssh/sshd_config.d
cat > /etc/ssh/sshd_config.d/10-qbone.conf <<'EOF'
PermitRootLogin no
PermitEmptyPasswords no
EOF
chmod 644 /etc/ssh/sshd_config.d/10-qbone.conf

# boot settings the cape needs, the same ones qbone-setup writes
U=/boot/uEnv.txt
set_uenv() {
    if grep -qE "^#?$1=" "$U"; then sed -i "s|^#\?$1=.*|$1=$2|" "$U"
    else echo "$1=$2" >> "$U"; fi
}
sed -i "s|^uboot_overlay_pru=|#uboot_overlay_pru=|" "$U" 2>/dev/null || true
for k in emmc video audio; do set_uenv "disable_uboot_overlay_$k" 1; done
set_uenv enable_uboot_overlays 1
grep -q "^uboot_overlay_addr4=QBone.dtbo$" "$U" || set_uenv uboot_overlay_addr4 QBone.dtbo

# eth0 as a plain NIC: build the legacy-Ethernet device tree from the on-image
# sources and install it as the DTB U-Boot loads. The kernel version comes from
# the rootfs, not uname (which under qemu reports the build host's kernel).
KVER=$(basename "$(ls -d /boot/dtbs/*/ | head -1)")
KMM=$(echo "$KVER" | cut -d. -f1-2)
SRC=/opt/source/dtb-${KMM}.x
BASE_DTB=am335x-boneblack-uboot.dtb
cp /tmp/in/qbone.dts "$SRC/src/arm/ti/omap/am335x-boneblack-qbone.dts"
( cd "$SRC" && make ARCH=arm CPP=cpp DTC=dtc \
    src/arm/ti/omap/am335x-boneblack-qbone.dtb >/dev/null 2>&1 )
DTB="$SRC/src/arm/ti/omap/am335x-boneblack-qbone.dtb"
[ -e "$DTB" ] || { echo "device tree build failed" >&2; exit 1; }
cp "/boot/dtbs/$KVER/$BASE_DTB" "/boot/dtbs/$KVER/$BASE_DTB.stock"
cp "$DTB" "/boot/dtbs/$KVER/$BASE_DTB"

# the sample operating systems and their boot configurations
install -d -m 755 /var/lib/qbone/images /var/lib/qbone/configs
cp /tmp/in/images/* /var/lib/qbone/images/
cp /tmp/in/configs/* /var/lib/qbone/configs/

rm -rf /tmp/in
CHROOT

echo "-- enabling the services (offline, from the host)"
# qbone-setup.service runs qbone-setup --auto on first boot so the image
# configures its network bridge with no login; the image enables it, a package
# install leaves it disabled
systemctl --root=/mnt enable qbone-network.service qbone.service qbone-setup.service qbone-leds.service >/dev/null 2>&1 || true
# the USB gadget serial getty spins on a tty that is not reliably present and
# wedges the console; the appliance is reached over the physical UART, the web
# interface and ssh. Mask the GPIO daemon's unit too - nothing here uses it.
systemctl --root=/mnt mask serial-getty@ttyGS0.service gpio-manager.service >/dev/null 2>&1 || true
# a persistent journal survives reboots
mkdir -p /mnt/var/log/journal

echo "-- resetting identity"
# restore the managed resolv.conf symlink the base image shipped
if [ -n "$RESOLV_LINK" ]; then
    rm -f /mnt/etc/resolv.conf
    ln -s "$RESOLV_LINK" /mnt/etc/resolv.conf
fi
: > /mnt/etc/machine-id
rm -f /mnt/etc/ssh/ssh_host_*
echo qbone > /mnt/etc/hostname
sed -i 's/\bBeagleBone\b/qbone/g; s/127\.0\.1\.1.*/127.0.1.1\tqbone/' /mnt/etc/hosts 2>/dev/null || true
rm -f /mnt/var/lib/qbone/settings.json
find /mnt/var/log -type f -exec truncate -s0 {} + 2>/dev/null || true
rm -rf /mnt/var/lib/apt/lists/* /mnt/var/cache/apt/archives/*.deb
rm -f /mnt/root/.bash_history /mnt/home/*/.bash_history 2>/dev/null || true

sync
umount -R /mnt
trap - EXIT

echo "-- shrinking the root filesystem to fit"
e2fsck -pf "${LO}p3" >/dev/null 2>&1 || true
# minimum, then add the margin
MINBLK=$(resize2fs -P "${LO}p3" 2>/dev/null | awk '{print $NF}')
BLKSZ=$(dumpe2fs -h "${LO}p3" 2>/dev/null | awk -F: '/Block size/{print $2}' | tr -d ' ')
TARGET=$(( MINBLK + MARGIN_MB * 1024 * 1024 / BLKSZ ))
resize2fs "${LO}p3" "$TARGET" >/dev/null
e2fsck -pf "${LO}p3" >/dev/null 2>&1 || true

# resize the partition to the filesystem and truncate the image to the
# partition end, so the file is only as large as its content
FS_BYTES=$(( TARGET * BLKSZ ))
P3_START=$(partx -g -o START -n 3:3 "$LO" | tr -d ' ')
losetup -d "$LO"
NEW_SECTORS=$(( (FS_BYTES + 511) / 512 ))
echo ", ${NEW_SECTORS}" | sfdisk -q -N 3 /work.img >/dev/null
END_SECTOR=$(( P3_START + NEW_SECTORS ))
truncate -s $(( END_SECTOR * 512 )) /work.img

cp /work.img /dist/qbone-dist.img
echo "-- done: $(du -h /dist/qbone-dist.img | cut -f1)"
CONTAINER

mv "$DIST/qbone-dist.img" "$OUT"
echo "== image ready: $OUT ($(du -h "$OUT" | cut -f1)) =="
