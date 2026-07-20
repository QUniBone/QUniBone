#!/bin/sh
# pdpsession.sh -- drive the PDP-11 console over QBone's /ws/console/ext
# websocket, in one connection, with paced keystrokes.
#
# The browser console wedges under diagnostic load, so long unattended runs
# (CZQNA and friends) are driven from here instead.
#
# Usage: pdpsession.sh [-H host] [-o log] [-p pace] [-w pwfile] step...
#
# Each step is applied in order:
#   sleep:N   wait N seconds before the next step
#   ctrl:X    send the control character for X (ctrl:C interrupts)
#   text      send text followed by a carriage return
#   ""        send a bare carriage return (accept a prompt default)
#
# Console output goes to stdout, and to the log file when -o names one. The
# connection closes after the last step, so end with a sleep long enough to
# capture the tail of the run.
#
# Once the interface has an admin password the websocket wants it too, and
# refuses with 401 without it. The password comes from -w, from
# QBONE_PASSWORD, or from ~/.qbone-pw when that exists - never from the
# command line, where it would sit in the shell history. Any user name is
# accepted, so only the password matters.
#
# Example -- a full CZQNA pass from a halted machine:
#   pdpsession.sh -o run.log "sleep:32" "B DL0" "sleep:25" "R ZQNAJ0" \
#     "sleep:14" "STA" "sleep:5" "Y" "sleep:4" "1" "sleep:4" "" "sleep:3" \
#     "" "sleep:3" "N" "sleep:3" "N" "sleep:3" "Y" "sleep:6" "N" "sleep:85"

host=192.168.2.223
log=
pace=0.04
pwfile=${QBONE_PASSWORD_FILE:-$HOME/.qbone-pw}

while getopts H:o:p:w: opt; do
    case $opt in
        H) host=$OPTARG ;;
        o) log=$OPTARG ;;
        p) pace=$OPTARG ;;
        w) pwfile=$OPTARG ;;
        *) echo "usage: $0 [-H host] [-o log] [-p pace] [-w pwfile] step..." >&2; exit 2 ;;
    esac
done
shift $((OPTIND - 1))

# websocat's --basic-auth takes "user:password" and encodes it itself, whatever
# its --help says about the argument being base64.
password=$QBONE_PASSWORD
if [ -z "$password" ] && [ -r "$pwfile" ]; then
    password=$(cat "$pwfile")
fi
if [ -n "$password" ]; then
    auth="--basic-auth qbone:$password"
else
    auth=
fi

if [ $# -eq 0 ]; then
    echo "$0: no steps given" >&2
    exit 2
fi

# One character at a time: the console firmware drops input sent faster than
# it polls, and a dropped character derails the whole answer dialogue.
send() {
    awk -v s="$1" -v p="$pace" 'BEGIN {
        s = s "\r"
        for (i = 1; i <= length(s); i++) {
            printf "%s", substr(s, i, 1)
            fflush()
            system("sleep " p)
        }
    }'
}

steps() {
    for step in "$@"; do
        case $step in
            sleep:*) sleep "${step#sleep:}" ;;
            ctrl:*)
                c=${step#ctrl:}
                awk -v c="$c" 'BEGIN {
                    printf "%c", index("ABCDEFGHIJKLMNOPQRSTUVWXYZ", toupper(c))
                    fflush()
                }'
                ;;
            *) send "$step" ;;
        esac
    done
}

if [ -n "$log" ]; then
    steps "$@" | websocat $auth -b "ws://$host/ws/console/ext" | tee "$log"
else
    steps "$@" | websocat $auth -b "ws://$host/ws/console/ext"
fi
