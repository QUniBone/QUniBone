#!/usr/bin/env python3
# Parse a macro11 listing and DMA it into the machine's memory through the
# QBone web API's /api/memory endpoint, then optionally start it from ODT.
# Far faster and more reliable than depositing words over the console.
import json, os, re, subprocess, sys, urllib.request

lst = sys.argv[1]
start = sys.argv[2] if len(sys.argv) > 2 else None   # octal start address to "G"

pw = open(os.path.expanduser("~/.qbone-pw")).read().strip()
base = "http://qbone"

# --- parse the listing into address -> bytes, same logic as the ODT loader ---
mem = {}
for line in open(lst):
    m = re.match(r'\s*\d+\s+([0-7]{6})\s+(.*)', line)
    if not m:
        continue
    addr = int(m.group(1), 8)
    rest = m.group(2)
    a = addr
    for tok in re.finditer(r'([0-7]{6}|[0-7]{3})', rest):
        t = tok.group(1)
        if rest[:tok.start()].strip() and not re.match(r'^[0-7\s]+$', rest[:tok.start()]):
            break
        if len(t) == 6:
            v = int(t, 8)
            # A trailing apostrophe marks a PC-relative operand: the listing shows
            # the resolved absolute target, but the object word holds the offset
            # from the word after it. jsr pc,X(pc) and any X(pc) mode land here.
            if rest[tok.end():tok.end() + 1] == "'":
                v = (v - (a + 2)) & 0xFFFF
            mem[a] = v & 0xFF; mem[a + 1] = (v >> 8) & 0xFF; a += 2
        else:
            mem[a] = int(t, 8) & 0xFF; a += 1

if not mem:
    sys.exit("no code parsed")

lo, hi = min(mem) & ~1, max(mem)
words = [mem.get(a, 0) | (mem.get(a + 1, 0) << 8) for a in range(lo, hi + 1, 2)]

# --- one DMA write of the whole program ---
body = json.dumps({"address": lo, "words": words}).encode()
req = urllib.request.Request(base + "/api/memory", data=body, method="POST")
import base64
req.add_header("Authorization", "Basic " + base64.b64encode(b":" + pw.encode()).decode())
req.add_header("Content-Type", "application/json")
resp = json.load(urllib.request.urlopen(req, timeout=30))
print(f"DMA-loaded {resp['count']} words at {lo:06o}")

# --- verify a couple of words by reading them back ---
rurl = f"{base}/api/memory?address={lo:o}&count=4"
rreq = urllib.request.Request(rurl)
rreq.add_header("Authorization", "Basic " + base64.b64encode(b":" + pw.encode()).decode())
rd = json.load(urllib.request.urlopen(rreq, timeout=30))
back = rd["words"]
ok = back[:4] == words[:4]
print("readback", "matches" if ok else f"MISMATCH {[oct(x) for x in back]} vs {[oct(x) for x in words[:4]]}")

# --- start it from ODT if asked ---
if start:
    odt = os.path.join(os.path.dirname(os.path.abspath(__file__)), "odt.py")
    out = subprocess.run(["python3", odt, f"{start}G"], capture_output=True, text=True, timeout=60)
    sys.stdout.write(out.stdout[-200:])
