#!/usr/bin/env python3
"""Paint a test card on a VCB01 screen straight from the host.

QBone is bus master, so this DMAs a picture into the board's video memory
through the web API's /api/memory endpoint - no program runs on the PDP-11,
and the CPU need not even be present. It writes the bitmap, a linear scanline
map, and the control register to turn video on.

The VCB01 must already be enabled with a display (set its "display" parameter
and enable it in the web interface, or with the API).

usage:
    tools/vcb01_testcard.py [--host qbone] [--password-file ~/.qbone-pw]
                            [--bank 16] [--cell 32]

The board must be reachable over HTTP; the password is the web admin password
(any user name), read from --password-file, $QBONE_PASSWORD, or --password.
"""
import argparse, base64, json, os, sys, urllib.request

# Video memory layout, from 10.02_devices/2_src/vcb01_render.hpp.
XSIZE, YSIZE = 1024, 864
LINE_BYTES = XSIZE // 8             # 128 bytes per buffer line
MAP_OFFSET = 0xFE00 * 4            # scanline map: one word per screen line
CSR_OFFSET = 0o17777200           # control/status register, absolute I/O-page addr
MAX_WORDS = 4096                   # per /api/memory request


def main():
    ap = argparse.ArgumentParser(description="Paint a VCB01 test card via the memory API.")
    ap.add_argument("--host", default=os.environ.get("QBONE_HOST", "qbone"),
                    help="board host name (default: qbone, or $QBONE_HOST)")
    ap.add_argument("--password-file", default=os.path.expanduser("~/.qbone-pw"),
                    help="file holding the web admin password")
    ap.add_argument("--password", default=os.environ.get("QBONE_PASSWORD"),
                    help="web admin password (overrides --password-file)")
    ap.add_argument("--bank", type=lambda s: int(s, 8), default=0o16,
                    help="video memory bank in octal (default 16 -> 16000000)")
    ap.add_argument("--cell", type=int, default=32,
                    help="grid cell size in pixels (default 32, which tiles 1024x864)")
    args = ap.parse_args()

    password = args.password
    if password is None:
        try:
            password = open(args.password_file).read().strip()
        except OSError as e:
            sys.exit(f"no password: {e} (use --password or $QBONE_PASSWORD)")
    auth = "Basic " + base64.b64encode(f":{password}".encode()).decode()
    base_url = f"http://{args.host}"
    bank = args.bank << 18

    def post_words(addr, words):
        for off in range(0, len(words), MAX_WORDS):
            body = json.dumps({"address": addr + 2 * off,
                               "words": words[off:off + MAX_WORDS]}).encode()
            req = urllib.request.Request(base_url + "/api/memory", data=body, method="POST")
            req.add_header("Authorization", auth)
            req.add_header("Content-Type", "application/json")
            try:
                json.load(urllib.request.urlopen(req, timeout=30))
            except urllib.error.URLError as e:
                sys.exit(f"POST /api/memory failed: {e}")

    # --- draw a 1-bpp test card ------------------------------------------
    # bit x of a line lives in byte x//8, bit x%8 (LSB leftmost).
    fb = [bytearray(LINE_BYTES) for _ in range(YSIZE)]

    def setpix(x, y):
        if 0 <= x < XSIZE and 0 <= y < YSIZE:
            fb[y][x >> 3] |= 1 << (x & 7)

    # regular square grid plus a border
    for y in range(YSIZE):
        for x in range(XSIZE):
            if (x % args.cell == 0 or y % args.cell == 0
                    or x == XSIZE - 1 or y == YSIZE - 1):
                setpix(x, y)

    # two solid diagonals, drawn along the longer (x) axis so every column
    # gets a pixel and the line is continuous
    for x in range(XSIZE):
        y = x * (YSIZE - 1) // (XSIZE - 1)
        setpix(x, y)
        setpix(x, YSIZE - 1 - y)

    # a crosshair through the centre
    for x in range(XSIZE):
        setpix(x, YSIZE // 2)
    for y in range(YSIZE):
        setpix(XSIZE // 2, y)

    # --- pack and DMA the bitmap -----------------------------------------
    def line_words(line):
        return [line[2 * i] | (line[2 * i + 1] << 8) for i in range(LINE_BYTES // 2)]

    bitmap = []
    for line in fb:
        bitmap += line_words(line)
    post_words(bank, bitmap)
    print(f"bitmap: {len(bitmap)} words at {bank:o}")

    # --- linear scanline map: screen line N shows buffer line N ----------
    post_words(bank + MAP_OFFSET, list(range(YSIZE)))
    print(f"scanline map: {YSIZE} words at {bank + MAP_OFFSET:o}")

    # --- turn video on (CSR VID = 4) -------------------------------------
    post_words(CSR_OFFSET, [0o4])
    print("video enabled; test card is on screen")


if __name__ == "__main__":
    main()
