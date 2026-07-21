#!/bin/bash
# personalize-image.sh - layer a personal account onto a QBone image.
#
# build-image.sh produces the distributable image, which carries no personal
# data. This adds one account to a COPY of it - your login, your ssh public
# key, zsh as the shell, passwordless sudo - and writes a new image. The
# distributable is never modified, and nothing personal enters the repository:
# the account name and key come from the command line and from files you keep
# outside the tree.
#
# The account is key-only: no password is set, so it is reached over ssh with
# your key (and has passwordless sudo). The image's own default account still
# serves for console login. Set a password yourself if you want console login
# as this user.
#
# usage:
#   PUSER=hans PUSER_KEY=~/.ssh/id_ed25519.pub \
#       ./packaging/personalize-image.sh qbone-dist.img qbone-hans.img
#
# optional environment:
#   PUSER_SHELL   login shell, must already be in the image (default /usr/bin/zsh)
#   PUSER_HOOK    a script run inside the image's chroot after the account is
#                 made, for anything else - dotfiles, extra packages, locking
#                 the default account. Its directory is mounted read-only at
#                 /personal in the chroot, so the hook can copy files from there.

set -euo pipefail

IN=${1:-}; OUT=${2:-}
[ -n "$IN" ] && [ -n "$OUT" ] || { echo "usage: personalize-image.sh IN.img OUT.img" >&2; exit 1; }
: "${PUSER:?set PUSER to your account name}"
: "${PUSER_KEY:?set PUSER_KEY to your ssh public key file}"
export PUSER_SHELL=${PUSER_SHELL:-/usr/bin/zsh}
[ -r "$IN" ] || { echo "no input image: $IN" >&2; exit 1; }
[ -r "$PUSER_KEY" ] || { echo "cannot read ssh key: $PUSER_KEY" >&2; exit 1; }
export PUSER

KEYDIR=$(cd "$(dirname "$PUSER_KEY")" && pwd); KEYFILE=$(basename "$PUSER_KEY")
HOOKMOUNT=(); export HOOKNAME=""
if [ -n "${PUSER_HOOK:-}" ]; then
    [ -r "$PUSER_HOOK" ] || { echo "cannot read hook: $PUSER_HOOK" >&2; exit 1; }
    HOOKDIR=$(cd "$(dirname "$PUSER_HOOK")" && pwd); HOOKNAME=$(basename "$PUSER_HOOK")
    HOOKMOUNT=(-v "$HOOKDIR":/personal:ro)
fi

echo "== copying $IN -> $OUT =="
cp "$IN" "$OUT"
OUTDIR=$(cd "$(dirname "$OUT")" && pwd); export OUTNAME=$(basename "$OUT")

echo "== adding account '$PUSER' (shell $PUSER_SHELL) in a privileged container =="
docker run --rm -i --privileged \
    -e PUSER -e PUSER_SHELL -e HOOKNAME -e OUTNAME \
    -v "$OUTDIR":/out \
    -v "$KEYDIR/$KEYFILE":/in/authorized_key:ro \
    ${HOOKMOUNT[@]+"${HOOKMOUNT[@]}"} \
    debian:trixie bash -euo pipefail <<'C'
export DEBIAN_FRONTEND=noninteractive
apt-get -qq update >/dev/null
apt-get -qq install -y util-linux qemu-user-static >/dev/null

LO=$(losetup -Pf --show /out/"$OUTNAME"); LB=$(basename "$LO")
trap 'umount -R /m 2>/dev/null || true; losetup -d "$LO" 2>/dev/null || true' EXIT
# no udev in the container: create the partition device nodes from /sys
for sp in /sys/block/"$LB"/"$LB"p*; do
    n=$(basename "$sp"); IFS=: read -r a b < "$sp/dev"; [ -e /dev/$n ] || mknod /dev/$n b "$a" "$b"
done
mkdir -p /m; mount "${LO}p3" /m
mount -t proc proc /m/proc; mount --bind /dev /m/dev

if ! chroot /m test -x "$PUSER_SHELL"; then
    echo "shell $PUSER_SHELL is not in the image - add it to build-image.sh first" >&2
    exit 1
fi
cp /in/authorized_key /m/tmp/authorized_key
# expose the hook's directory to the chroot read-only by bind mount, so the
# hook can read assets from /personal without copying anything into the image
if [ -n "$HOOKNAME" ]; then mkdir -p /m/personal; mount --bind /personal /m/personal; fi

# the chroot inherits PUSER, PUSER_SHELL and HOOKNAME from this environment
chroot /m /bin/bash -euo pipefail <<'CHR'
export DEBIAN_FRONTEND=noninteractive
if ! id "$PUSER" >/dev/null 2>&1; then
    SUPP=sudo
    getent group admin >/dev/null 2>&1 && SUPP=$SUPP,admin
    useradd -m -s "$PUSER_SHELL" -G "$SUPP" "$PUSER"
fi
usermod -s "$PUSER_SHELL" "$PUSER"
install -d -m 700 -o "$PUSER" -g "$PUSER" /home/"$PUSER"/.ssh
install -m 600 -o "$PUSER" -g "$PUSER" /tmp/authorized_key /home/"$PUSER"/.ssh/authorized_keys
rm -f /tmp/authorized_key
# zz- prefix so this wins over the image's own sudoers (last match wins)
printf '%s ALL=(ALL) NOPASSWD:ALL\n' "$PUSER" > /etc/sudoers.d/zz-"$PUSER"
chmod 440 /etc/sudoers.d/zz-"$PUSER"
if [ -n "$HOOKNAME" ] && [ -x /personal/"$HOOKNAME" ]; then /personal/"$HOOKNAME"; fi
CHR

if [ -n "$HOOKNAME" ]; then umount /m/personal; rmdir /m/personal; fi
sync
umount -R /m; losetup -d "$LO"; trap - EXIT
echo "-- account added"
C

echo "== done: $OUT =="
