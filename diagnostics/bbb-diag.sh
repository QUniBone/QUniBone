#!/bin/bash
# bbb-diag.sh -- collect why a BeagleBone has wedged, when systemd cannot be
# asked.
#
# Run as root, ideally while the machine is misbehaving. Everything that talks
# to systemd is wrapped in a timeout, so a blocked PID 1 cannot hang this
# script; those sections simply report that they timed out, which is itself
# the answer.
#
# Writes to /var/log/bbb-diag-<timestamp>.txt and to stdout.
#
# Needs bash: the output is teed through a process substitution.
#
# usage:  sudo bash bbb-diag.sh

[ "$(id -u)" = 0 ] || { echo "run as root" >&2; exit 1; }

OUT=/var/log/bbb-diag-$(date +%Y%m%d-%H%M%S).txt
exec > >(tee "$OUT") 2>&1

sec() { echo; echo "=================== $* ==================="; }
# systemd may be blocked; never wait on it
sd() { timeout 5 "$@" 2>&1 || echo "  [timed out or failed: $*]"; }

echo "bbb-diag $(date -Is)"
uname -a
uptime

# ---------------------------------------------------------------- storage
# First, because a blocked PID 1 is usually blocked on I/O, and every other
# symptom is downstream of that.
sec "MOUNTS (is / read-only?)"
mount | grep -E ' / | /var | /boot '
sec "DISK SPACE"
df -h / /var /run /tmp 2>/dev/null
sec "STORAGE AND FILESYSTEM ERRORS IN DMESG"
dmesg | grep -iE "mmc|sdhci|I/O error|EXT4-fs (error|warning)|remount|Buffer I/O" | tail -40
echo "  (no output above means no storage errors were logged)"

# ------------------------------------------------------------- hung tasks
sec "HUNG TASK REPORTS"
dmesg | grep -B2 -A30 "blocked for more than" | tail -60
echo "  (no output above means the hung-task detector has not fired)"

sec "PROCESSES IN UNINTERRUPTIBLE SLEEP (D state)"
ps -eo pid,ppid,stat,wchan:40,comm | awk 'NR==1 || $3 ~ /D/'

sec "PID 1"
cat /proc/1/wchan 2>/dev/null; echo
echo "-- stack --"
cat /proc/1/stack 2>/dev/null || echo "  (kernel stack not exposed)"
echo "-- status --"
grep -E "^(State|Threads|VmRSS)" /proc/1/status 2>/dev/null

# Ask the kernel to dump every blocked task. Needs sysrq enabled; restore
# whatever it was set to afterwards.
sec "SYSRQ BLOCKED-TASK DUMP"
SYSRQ_WAS=$(cat /proc/sys/kernel/sysrq 2>/dev/null)
echo 1 > /proc/sys/kernel/sysrq 2>/dev/null
echo w > /proc/sysrq-trigger 2>/dev/null && sleep 1 && dmesg | tail -60 \
    || echo "  (sysrq unavailable)"
[ -n "$SYSRQ_WAS" ] && echo "$SYSRQ_WAS" > /proc/sys/kernel/sysrq 2>/dev/null

# ---------------------------------------------------------------- systemd
# Each of these hanging is a finding, not a failure of the script.
sec "SYSTEMD: IS IT ANSWERING?"
sd systemctl is-system-running
sec "SYSTEMD: RUNNING/QUEUED JOBS"
sd systemctl list-jobs
sec "SYSTEMD: FAILED UNITS"
sd systemctl --failed --no-pager
sec "SESSIONS"
sd loginctl list-sessions
sec "UNIT STATE: logind, getty, user manager"
for u in systemd-logind.service serial-getty@ttyS0.service \
         "user@$(id -u "${SUDO_USER:-root}" 2>/dev/null || echo 0).service"; do
    echo "-- $u --"
    sd systemctl status "$u" --no-pager -n 15
done

# ----------------------------------------------------------------- memory
sec "MEMORY AND SWAP"
free -h
cat /proc/swaps
sec "OUT OF MEMORY EVENTS"
dmesg | grep -iE "out of memory|killed process|oom-kill" | tail -20
echo "  (no output above means the OOM killer has not run)"

# ------------------------------------------------------------ package state
sec "INTERRUPTED PACKAGE OPERATIONS"
dpkg --audit 2>&1 | head -20
echo "  (no output above means dpkg is consistent)"

# ------------------------------------------------------------------- cape
sec "CAPE AND PRU"
ls /dev/uio* 2>/dev/null || echo "  no /dev/uio* (expected on 6.12: uio_pruss was removed in 6.10)"
ls /sys/class/remoteproc/ 2>/dev/null || echo "  no remoteproc devices"
grep -E "^(uname_r|disable_uboot_overlay_emmc|uboot_overlay_pru|uboot_overlay_addr4)" \
    /boot/uEnv.txt 2>/dev/null

sec "RECENT KERNEL MESSAGES"
dmesg | tail -40

echo
echo "written to $OUT"
