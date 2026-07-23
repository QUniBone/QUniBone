#!/usr/bin/env python3
# Send a sequence of micro-ODT commands to the PDP-11 console over the
# QBone external-console WebSocket, pacing characters, and print what comes
# back. Each argument is one ODT line; CR terminates it (deposit + close).
import base64, subprocess, sys, time, os

pw = open(os.path.expanduser("~/.qbone-pw")).read().strip()
auth = base64.b64encode(f":{pw}".encode()).decode()
lines = sys.argv[1:]

# Build the input stream: each line's characters, then CR, paced by the shell
# side of websocat. We feed it all at once with small sleeps via a helper.
script = ""
for ln in lines:
    script += ln + "\r"

def gen():
    time.sleep(2)
    for ch in script:
        sys.stdout.write(ch)
        sys.stdout.flush()
        time.sleep(0.05)
    time.sleep(3)

if os.environ.get("ODT_GEN"):
    gen()
    sys.exit()

# parent: run the generator piped into websocat, capture output
p = subprocess.Popen(
    f'ODT_GEN=1 python3 {sys.argv[0]} ' + " ".join(f'"{l}"' for l in lines) +
    f' | websocat --binary -H="Authorization: Basic {auth}" ws://qbone/ws/console/ext',
    shell=True, stdout=subprocess.PIPE, stderr=subprocess.DEVNULL)
out, _ = p.communicate(timeout=60)
sys.stdout.write(out.decode("latin-1"))
