/* vcb01_selftest.cpp: exercise the QVSS renderer without a PDP-11

   Copyright (c) 2026, Hans Huebner

   Permission is hereby granted, free of charge, to any person obtaining a
   copy of this software and associated documentation files (the "Software"),
   to deal in the Software without restriction, including without limitation
   the rights to use, copy, modify, merge, publish, distribute, sublicense,
   and/or sell copies of the Software, and to permit persons to whom the
   Software is furnished to do so, subject to the following conditions:

   The above copyright notice and this permission notice shall be included in
   all copies or substantial portions of the Software.

   THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
   IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
   FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
   THE AUTHORS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
   IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
   CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

   Builds a synthetic 256 KB video bank, drives the renderer over it, and
   checks that pixels land where the board would put them. With --display it
   also opens a window and animates, which is how the scanline map and the
   cursor get looked at rather than only asserted.

   usage:
     vcb01_selftest                       checks only, no X server needed
     vcb01_selftest --display <name>      checks, then animate on that display
     vcb01_selftest --resolve <name>      print what a display name resolves to
     vcb01_selftest --dump <file.bmp>     render one frame to an image
     vcb01_selftest --ddrmem [n] [0xoff] [f.bmp]
                                          render from the board's own video
                                          memory and time it (needs root)
*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <errno.h>
#include <string>
#include <vector>
#include <set>
#include <time.h>

#include "vcb01_render.hpp"
#include "x11display.hpp"

using namespace vcb01;

static int failures = 0;

static void check(bool ok, const char *what)
{
    printf("  %-58s %s\n", what, ok ? "ok" : "FAILED");
    if (!ok)
        failures++;
}

// The screen lines a set of spans covers, for asserting on exactly which
// lines a change dirtied.
static std::set<unsigned> dirty_lines(const std::vector<span_t> &spans)
{
    std::set<unsigned> lines;
    for (const span_t &s : spans)
        for (unsigned i = 0; i < s.count; i++)
            lines.insert(s.first + i);
    return lines;
}

// ------------------------------------------------------------ bank authoring

// Set one pixel of a buffer line. Bit 0 of a byte is its leftmost pixel.
static void set_pixel(uint8_t *bank, unsigned buffer_line, unsigned x, bool on)
{
    size_t byte = (size_t) buffer_line * LINE_BYTES + x / 8;
    uint8_t mask = (uint8_t) (1u << (x % 8));
    if (on)
        bank[byte] |= mask;
    else
        bank[byte] &= (uint8_t) ~mask;
}

// Point a screen line at a buffer line: 11 bits, two entries per longword,
// even screen line in the low half.
static void set_map(uint8_t *bank, unsigned screen_line, unsigned buffer_line)
{
    uint8_t *p = bank + MAP_LW * 4 + (screen_line / 2) * 4 + (screen_line & 1) * 2;
    p[0] = (uint8_t) (buffer_line & 0xFF);
    p[1] = (uint8_t) ((buffer_line >> 8) & 0x07);
}

static void set_cursor_pixel(uint8_t *bank, unsigned row, unsigned col, bool on)
{
    unsigned i = row * CURSOR_SIZE + col;
    uint8_t *p = bank + CURSOR_LW * 4 + i / 8;
    uint8_t mask = (uint8_t) (1u << (i % 8));
    if (on)
        *p |= mask;
    else
        *p &= (uint8_t) ~mask;
}

static void identity_map(uint8_t *bank)
{
    for (unsigned line = 0; line < YSIZE; line++)
        set_map(bank, line, line);
}

// ------------------------------------------------------------ checks

static void run_checks()
{
    std::vector<uint8_t> bank(BANK_BYTES, 0);
    renderer_c r;
    state_t st;
    st.video_enable = true;

    printf("bit order and geometry\n");

    identity_map(bank.data());
    set_pixel(bank.data(), 0, 0, true);         // leftmost pixel of line 0
    set_pixel(bank.data(), 0, 7, true);         // last pixel of the first byte
    set_pixel(bank.data(), 0, 1023, true);      // rightmost pixel
    set_pixel(bank.data(), 863, 0, true);       // bottom left

    r.update(bank.data(), st);
    const uint8_t *px = r.pixels();

    check(px[0] == 1, "bit 0 of the first byte is the leftmost pixel");
    check(px[1] == 0 && px[6] == 0, "bits 1..6 clear between the two set bits");
    check(px[7] == 1, "bit 7 of the first byte is the eighth pixel");
    check(px[1023] == 1, "last pixel of a line comes from the last byte");
    check(px[(size_t) 863 * XSIZE] == 1, "line stride is 128 bytes");
    check(px[XSIZE] == 0, "line 1 is unaffected by writes to line 0");

    printf("scanline map\n");

    // Point every screen line at buffer line 0, which the board does to show
    // one line down the whole screen.
    for (unsigned line = 0; line < YSIZE; line++)
        set_map(bank.data(), line, 0);
    r.update(bank.data(), st);
    px = r.pixels();
    check(px[0] == 1 && px[(size_t) 500 * XSIZE] == 1,
            "a buffer line shows on every screen line pointing at it");

    // Entry width: 11 bits reaches buffer line 2031.
    identity_map(bank.data());
    set_pixel(bank.data(), 2031, 4, true);
    set_map(bank.data(), 10, 2031);
    r.update(bank.data(), st);
    px = r.pixels();
    check(px[(size_t) 10 * XSIZE + 4] == 1, "an 11-bit map entry reaches buffer line 2031");

    // An entry past the bitmap shows nothing instead of the map itself.
    set_map(bank.data(), 11, 2047);
    r.update(bank.data(), st);
    px = r.pixels();
    bool blank = true;
    for (unsigned x = 0; x < XSIZE; x++)
        if (px[(size_t) 11 * XSIZE + x] != 0)
            blank = false;
    check(blank, "a map entry past the bitmap paints nothing");

    printf("dirty tracking\n");

    identity_map(bank.data());
    r.invalidate_all();
    r.update(bank.data(), st);

    const std::vector<span_t> &none = r.update(bank.data(), st);
    check(none.empty(), "an unchanged bank reports no dirty lines");

    set_pixel(bank.data(), 100, 10, true);
    const std::vector<span_t> &one = r.update(bank.data(), st);
    check(one.size() == 1 && one[0].first == 100 && one[0].count == 1,
            "one changed buffer line dirties exactly its screen line");

    // Three screen lines show buffer line 300: the two pointed at it here,
    // and line 300 itself, which the identity map still names.
    set_map(bank.data(), 200, 300);
    set_map(bank.data(), 201, 300);
    r.update(bank.data(), st);
    set_pixel(bank.data(), 300, 50, true);
    check(dirty_lines(r.update(bank.data(), st)) == std::set<unsigned> { 200, 201, 300 },
            "a shared buffer line dirties every screen line showing it");

    printf("cursor\n");

    identity_map(bank.data());
    for (unsigned row = 0; row < CURSOR_SIZE; row++)
        for (unsigned col = 0; col < CURSOR_SIZE; col++)
            set_cursor_pixel(bank.data(), row, col, true);   // solid block

    st.cursor_x = 40;
    st.cursor_y = 50;
    st.cursor_visible = true;
    st.cursor_or = true;
    r.invalidate_all();
    r.update(bank.data(), st);
    px = r.pixels();
    check(px[(size_t) 50 * XSIZE + 40] == 1 && px[(size_t) 65 * XSIZE + 55] == 1,
            "an OR cursor paints itself into the picture");
    check(px[(size_t) 50 * XSIZE + 39] == 0, "the cursor stops at its left edge");
    check(px[(size_t) 66 * XSIZE + 40] == 0, "the cursor is 16 rows tall");

    // AND cuts the cursor out of a set background.
    for (unsigned line = 50; line < 70; line++)
        for (unsigned x = 0; x < 200; x += 1)
            set_pixel(bank.data(), line, x, true);
    st.cursor_or = false;
    r.invalidate_all();
    r.update(bank.data(), st);
    px = r.pixels();
    check(px[(size_t) 55 * XSIZE + 45] == 0, "an AND cursor cuts itself out");
    check(px[(size_t) 55 * XSIZE + 20] == 1, "the background outside it survives");

    st.cursor_visible = false;
    r.invalidate_all();
    r.update(bank.data(), st);
    px = r.pixels();
    check(px[(size_t) 55 * XSIZE + 45] == 1, "an invisible cursor leaves the picture alone");

    printf("video enable\n");
    st.cursor_visible = false;
    st.video_enable = false;
    r.invalidate_all();
    r.update(bank.data(), st);
    px = r.pixels();
    check(px[(size_t) 55 * XSIZE + 20] == 0, "video disabled blanks the screen");

    // The transition itself has to repaint: a driver that writes video memory
    // while video is off, then enables it, must see its pixels. Without
    // invalidating - the transition is the only thing that changed - the
    // screen has to come back on its own. (Found on the bus: pixels written
    // before enable stayed hidden.)
    set_pixel(bank.data(), 55, 300, true);
    r.update(bank.data(), st);          // still off: the write paints blank
    st.video_enable = true;
    r.update(bank.data(), st);          // enable, no invalidate
    px = r.pixels();
    check(px[(size_t) 55 * XSIZE + 20] == 1, "enabling video repaints existing content");
    check(px[(size_t) 55 * XSIZE + 300] == 1, "a pixel written while off shows once enabled");
}

// ------------------------------------------------------------ display name checks

static void run_resolve_checks()
{
    printf("display names\n");
    std::string out, err;

    check(x11_resolve_display_name("192.168.2.10:0", &out, &err) && out == "192.168.2.10:0",
            "a literal IPv4 address passes through");
    check(x11_resolve_display_name(":0", &out, &err) && out == ":0",
            "a local display passes through");
    check(x11_resolve_display_name("localhost:0.1", &out, &err) && out == "localhost:0.1",
            "localhost passes through with its screen number");
    check(x11_resolve_display_name("[::1]:0", &out, &err) && out == "[::1]:0",
            "a bracketed IPv6 literal passes through");
    check(x11_resolve_display_name("/tmp/launch-abc/org.xquartz:0", &out, &err)
            && out == "/tmp/launch-abc/org.xquartz:0",
            "a local socket path passes through");
    check(!x11_resolve_display_name("no-such-host.invalid:0", &out, &err)
            && err.find("no-such-host.invalid") != std::string::npos,
            "an unresolvable name fails and names the host");

    if (x11_resolve_display_name("localhost.:0", &out, &err))
        printf("  (localhost. resolved to %s)\n", out.c_str());
}

// ------------------------------------------------------------ animation

static void draw_test_card(uint8_t *bank)
{
    // Each buffer line carries its own number in binary along the left, a
    // moving diagonal, and a ruler, so a stride or bit-order mistake is
    // visible rather than merely wrong.
    for (unsigned bl = 0; bl < YSIZE; bl++) {
        for (unsigned bit = 0; bit < 12; bit++)
            if (bl & (1u << bit))
                set_pixel(bank, bl, 4 + bit * 3, true);

        set_pixel(bank, bl, 60 + (bl % 900), true);
        set_pixel(bank, bl, 61 + (bl % 900), true);

        for (unsigned x = 0; x < XSIZE; x += 64)
            set_pixel(bank, bl, x, true);

        if (bl % 64 == 0)
            for (unsigned x = 0; x < XSIZE; x++)
                set_pixel(bank, bl, x, true);
    }
}

static void draw_arrow_cursor(uint8_t *bank)
{
    static const char *art[CURSOR_SIZE] = {
        "X...............",
        "XX..............",
        "XXX.............",
        "XXXX............",
        "XXXXX...........",
        "XXXXXX..........",
        "XXXXXXX.........",
        "XXXXXXXX........",
        "XXXXXXXXX.......",
        "XXXXXX..........",
        "XXX.XXX.........",
        "XX..XXX.........",
        "X....XXX........",
        ".....XXX........",
        "......XX........",
        "................",
    };
    for (unsigned row = 0; row < CURSOR_SIZE; row++)
        for (unsigned col = 0; col < CURSOR_SIZE; col++)
            set_cursor_pixel(bank, row, col, art[row][col] == 'X');
}

// Write the rendered screen as a 24-bit BMP, so the picture can be looked at
// without an X server in the way.
static bool dump_bmp(const char *path, const uint8_t *pixels)
{
    FILE *f = fopen(path, "wb");
    if (f == nullptr)
        return false;

    const unsigned row_bytes = (XSIZE * 3 + 3) & ~3u;
    const unsigned pixel_bytes = row_bytes * YSIZE;
    const unsigned offset = 54;

    uint8_t header[54];
    memset(header, 0, sizeof(header));
    header[0] = 'B'; header[1] = 'M';
    unsigned size = offset + pixel_bytes;
    memcpy(header + 2, &size, 4);
    memcpy(header + 10, &offset, 4);
    unsigned dib = 40;
    memcpy(header + 14, &dib, 4);
    int w = (int) XSIZE, h = -(int) YSIZE;       // negative height: top down
    memcpy(header + 18, &w, 4);
    memcpy(header + 22, &h, 4);
    uint16_t planes = 1, bpp = 24;
    memcpy(header + 26, &planes, 2);
    memcpy(header + 28, &bpp, 2);
    memcpy(header + 34, &pixel_bytes, 4);
    fwrite(header, 1, sizeof(header), f);

    std::vector<uint8_t> row(row_bytes, 0);
    for (unsigned y = 0; y < YSIZE; y++) {
        const uint8_t *src = pixels + (size_t) y * XSIZE;
        for (unsigned x = 0; x < XSIZE; x++) {
            uint8_t v = src[x] ? 0xFF : 0x00;
            row[x * 3 + 0] = (uint8_t) (src[x] ? 0x30 : 0);     // B
            row[x * 3 + 1] = v;                                 // G
            row[x * 3 + 2] = (uint8_t) (src[x] ? 0x30 : 0);     // R
        }
        fwrite(row.data(), 1, row_bytes, f);
    }
    fclose(f);
    return true;
}

// The picture a --dump is taken of: the test card, scrolled by the scanline
// map, with the cursor placed over it.
static void render_sample(const char *path)
{
    std::vector<uint8_t> bank(BANK_BYTES, 0);
    identity_map(bank.data());
    draw_test_card(bank.data());
    draw_arrow_cursor(bank.data());

    // Scroll by 100 lines through the map, so the picture proves the
    // indirection is being followed rather than the bitmap read straight.
    for (unsigned line = 0; line < YSIZE; line++)
        set_map(bank.data(), line, (line + 100) % YSIZE);

    renderer_c r;
    state_t st;
    st.video_enable = true;
    st.cursor_visible = true;
    st.cursor_or = true;
    st.cursor_x = 500;
    st.cursor_y = 400;

    r.update(bank.data(), st);
    if (dump_bmp(path, r.pixels()))
        printf("wrote %s\n", path);
    else
        fprintf(stderr, "cannot write %s\n", path);
}

// ------------------------------------------------------------ real video memory

#ifdef __linux__

#include <fcntl.h>
#include <dirent.h>
#include <unistd.h>
#include <sys/mman.h>

// The emulated machine's memory is a reserved-memory region the cape overlay
// declares; find it the way the emulator does, by scanning for the node rather
// than assuming an address.
static bool find_reserved_memory(uint32_t *pa, uint32_t *size)
{
    static const char *node_dir = "/proc/device-tree/reserved-memory";
    static const char *node_name = "qbone-ddr";

    std::string reg;
    DIR *dir = opendir(node_dir);
    if (dir == nullptr)
        return false;
    struct dirent *de;
    size_t namelen = strlen(node_name);
    while ((de = readdir(dir)) != nullptr)
        if (!strncmp(de->d_name, node_name, namelen)
                && (de->d_name[namelen] == '\0' || de->d_name[namelen] == '@')) {
            reg = std::string(node_dir) + "/" + de->d_name + "/reg";
            break;
        }
    closedir(dir);
    if (reg.empty())
        return false;

    int fd = open(reg.c_str(), O_RDONLY);
    if (fd < 0)
        return false;
    uint8_t cells[8];
    ssize_t n = read(fd, cells, sizeof(cells));
    close(fd);
    if (n != (ssize_t) sizeof(cells))
        return false;

    *pa = (uint32_t) ((cells[0] << 24) | (cells[1] << 16) | (cells[2] << 8) | cells[3]);
    *size = (uint32_t) ((cells[4] << 24) | (cells[5] << 16) | (cells[6] << 8) | cells[7]);
    return true;
}

static double seconds_since(const struct timespec &start)
{
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    return (double) (now.tv_sec - start.tv_sec)
            + (double) (now.tv_nsec - start.tv_nsec) / 1e9;
}

// Render out of the reserved region the PRU serves bus cycles from, and report
// what a frame costs there. Reads of it miss the cache - the region is mapped
// no-map, so the kernel never gives it a cached mapping - which is the one
// cost in the refresh loop that cannot be guessed at.
static void run_ddrmem(unsigned frames, uint32_t fb_offset, const char *dump)
{
    uint32_t pa = 0, size = 0;
    if (!find_reserved_memory(&pa, &size)) {
        fprintf(stderr, "no qbone-ddr reserved memory: the cape overlay must declare it\n");
        return;
    }
    printf("reserved memory: %u bytes at 0x%08x\n", size, pa);
    if (fb_offset + BANK_BYTES > size) {
        fprintf(stderr, "a %u-byte bank at 0x%x does not fit\n", BANK_BYTES, fb_offset);
        return;
    }

    int fd = open("/dev/mem", O_RDWR | O_SYNC);
    if (fd < 0) {
        fprintf(stderr, "cannot open /dev/mem: %s (run as root)\n", strerror(errno));
        return;
    }
    void *base = mmap(nullptr, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, pa);
    close(fd);
    if (base == MAP_FAILED) {
        fprintf(stderr, "cannot map 0x%08x: %s\n", pa, strerror(errno));
        return;
    }

    uint8_t *bank = (uint8_t *) base + fb_offset;
    printf("video bank at bus address 0%o, %u bytes\n", fb_offset, BANK_BYTES);

    // Author the test card straight into the region the PRU would serve.
    memset(bank, 0, BANK_BYTES);
    identity_map(bank);
    draw_test_card(bank);
    draw_arrow_cursor(bank);

    renderer_c r;
    state_t st;
    st.video_enable = true;
    st.cursor_visible = true;
    st.cursor_or = true;
    st.cursor_x = 500;
    st.cursor_y = 400;

    r.update(bank, st);         // warm up: first pass paints everything

    // The uncached read on its own.
    std::vector<uint8_t> sink(BANK_BYTES);
    struct timespec t0;
    clock_gettime(CLOCK_MONOTONIC, &t0);
    for (unsigned i = 0; i < frames; i++)
        memcpy(sink.data(), bank, BANK_BYTES);
    double copy_s = seconds_since(t0);

    // A whole pass with nothing changing: the copy, the map, the line diff.
    clock_gettime(CLOCK_MONOTONIC, &t0);
    for (unsigned i = 0; i < frames; i++)
        r.update(bank, st);
    double idle_s = seconds_since(t0);

    // A pass with one line changed, which adds the paint of that line.
    clock_gettime(CLOCK_MONOTONIC, &t0);
    for (unsigned i = 0; i < frames; i++) {
        set_pixel(bank, 40, i % XSIZE, true);
        r.update(bank, st);
    }
    double busy_s = seconds_since(t0);

    double mb = (double) BANK_BYTES / (1024.0 * 1024.0);
    printf("\nover %u frames:\n", frames);
    printf("  read %u KB from DDR   %7.2f ms/frame   %6.1f MB/s\n",
            BANK_BYTES / 1024, copy_s * 1000.0 / frames, mb * frames / copy_s);
    printf("  full pass, unchanged %7.2f ms/frame   %6.1f%% of one core at 30 Hz\n",
            idle_s * 1000.0 / frames, idle_s / frames * 30.0 * 100.0);
    printf("  full pass, 1 line    %7.2f ms/frame   %6.1f%% of one core at 30 Hz\n",
            busy_s * 1000.0 / frames, busy_s / frames * 30.0 * 100.0);

    if (dump != nullptr) {
        r.invalidate_all();
        r.update(bank, st);
        if (dump_bmp(dump, r.pixels()))
            printf("\nwrote %s\n", dump);
    }

    munmap(base, size);
}

#endif  // __linux__

static void animate(const std::string &display)
{
    std::vector<uint8_t> bank(BANK_BYTES, 0);
    identity_map(bank.data());
    draw_test_card(bank.data());
    draw_arrow_cursor(bank.data());

    renderer_c r;
    x11display_c win;

    if (!win.open(display, "QBone VCB01", XSIZE, YSIZE)) {
        printf("\ncannot open display: %s\n", win.error().c_str());
        return;
    }
    printf("\ndisplay \"%s\" resolved to \"%s\"\n", display.c_str(),
            win.resolved_display().c_str());
    printf("scrolling via the scanline map, cursor follows a circle.\n");
    printf("close the window to finish.\n");

    state_t st;
    st.video_enable = true;
    st.cursor_visible = true;
    st.cursor_or = true;

    bool running = true;
    unsigned frame = 0;
    struct timespec sleep_time = { 0, 33 * 1000 * 1000 };    // ~30 Hz

    while (running) {
        for (const x11display_c::event_t &ev : win.poll_events()) {
            if (ev.kind == x11display_c::event_t::CLOSED)
                running = false;
            else if (ev.kind == x11display_c::event_t::EXPOSED)
                r.invalidate_all();
            else if (ev.kind == x11display_c::event_t::KEY_PRESS && ev.keysym == 'q')
                running = false;
        }

        // Scroll by rotating the scanline map, which is what the board does
        // instead of moving pixels.
        for (unsigned line = 0; line < YSIZE; line++)
            set_map(bank.data(), line, (line + frame) % YSIZE);

        // The cursor walks a circle; its function flips every few seconds so
        // AND and OR both get looked at.
        double angle = frame * 0.05;
        st.cursor_x = (unsigned) (XSIZE / 2 + 300 * cos(angle));
        st.cursor_y = (unsigned) (YSIZE / 2 + 300 * sin(angle));
        st.cursor_or = ((frame / 90) % 2) == 0;

        const std::vector<span_t> &spans = r.update(bank.data(), st);
        for (const span_t &s : spans)
            win.put_rows(r.pixels(), s.first, s.count);
        win.flush();

        frame++;
        nanosleep(&sleep_time, nullptr);
    }
    win.close();
}

int main(int argc, char **argv)
{
    // Unbuffered, so a crash leaves the checks that already ran on screen.
    // stdbuf cannot do this from outside: it works through LD_PRELOAD and
    // this binary is linked statically.
    setvbuf(stdout, nullptr, _IONBF, 0);

    std::string display;
    bool do_animate = false;

    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--display") && i + 1 < argc) {
            display = argv[++i];
            do_animate = true;
        } else if (!strcmp(argv[i], "--dump") && i + 1 < argc) {
            render_sample(argv[++i]);
            return 0;
#ifdef __linux__
        } else if (!strcmp(argv[i], "--ddrmem")) {
            unsigned frames = 100;
            uint32_t offset = 0x380000;         // CSR MA = 016, bus 16000000
            const char *dump = nullptr;
            while (i + 1 < argc && argv[i + 1][0] != '-') {
                const char *arg = argv[++i];
                if (strchr(arg, '.') != nullptr)
                    dump = arg;
                else if (arg[0] == '0' && arg[1] == 'x')
                    offset = (uint32_t) strtoul(arg, nullptr, 16);
                else
                    frames = (unsigned) strtoul(arg, nullptr, 10);
            }
            run_ddrmem(frames, offset, dump);
            return 0;
#endif
        } else if (!strcmp(argv[i], "--resolve") && i + 1 < argc) {
            std::string out, err;
            if (x11_resolve_display_name(argv[++i], &out, &err))
                printf("%s\n", out.c_str());
            else {
                fprintf(stderr, "%s\n", err.c_str());
                return 1;
            }
            return 0;
        } else {
            fprintf(stderr, "usage: %s [--display <name>] [--resolve <name>]\n", argv[0]);
            return 2;
        }
    }

    run_checks();
    run_resolve_checks();

    printf("\n%d check(s) failed\n", failures);

    if (do_animate && failures == 0)
        animate(display);

    return failures == 0 ? 0 : 1;
}
