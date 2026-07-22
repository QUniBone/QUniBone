/* vcb01_render.hpp: QVSS (VCB01) video memory to pixels

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

   Turns the 256 KB the QVSS occupies in Q22 space into a one-byte-per-pixel
   image, and reports which screen lines changed since the last pass.

   The board has no way to tell the host that video memory was written - the
   PRU serves those bus cycles on its own - so each pass compares the bank
   against a shadow copy taken on the previous one.

   Layout of the bank, by longword index:

     0x0000..0xFDFF  bitmap, 32 longwords (128 bytes) per buffer line.
                     Bit 0 of a longword is its leftmost pixel.
     0xFE00..0xFFF7  scanline map: which buffer line appears on each screen
                     line. 11 bits per entry, two entries per longword, the
                     even screen line in the low half.
     0xFFF8..0xFFFF  16 x 16 cursor bitmap, 256 bits, LSB of each byte first.

   Several screen lines may name one buffer line, which is how the board
   scrolls without moving pixels, so a buffer line that changes dirties every
   screen line pointing at it.

   This file deliberately depends on nothing but the C++ standard library, so
   it builds and runs on a workstation against a synthetic bank.
*/
#ifndef _VCB01_RENDER_HPP_
#define _VCB01_RENDER_HPP_

#include <stdint.h>
#include <stddef.h>
#include <vector>

namespace vcb01 {

// The screen the board drives.
static const unsigned XSIZE = 1024;
static const unsigned YSIZE = 864;

// One buffer line is 1024 pixels, one bit each.
static const unsigned LINE_BYTES = XSIZE / 8;           // 128
static const unsigned BANK_BYTES = 256 * 1024;

// Longword indices where the bitmap gives way to the scanline map and the
// cursor image.
static const unsigned MAP_LW = 0xFE00;
static const unsigned CURSOR_LW = 0xFFF8;

// Buffer lines that fit below the scanline map.
static const unsigned BUFFER_LINES = (MAP_LW * 4) / LINE_BYTES;   // 2032

static const unsigned CURSOR_SIZE = 16;

// What the registers contribute to the picture. The renderer reads video
// memory itself and takes everything else from here.
struct state_t {
    unsigned cursor_x = 0;          // CSR cursor X, already masked to 10 bits
    unsigned cursor_y = 0;          // derived from the CRTC registers
    bool cursor_visible = false;    // CRTC cursor scan start bit 5 clear
    bool cursor_or = false;         // CSR FNC: true ORs the cursor in, false ANDs it out
    bool video_enable = true;       // CSR VID
};

// A run of consecutive screen lines that changed.
struct span_t {
    unsigned first;
    unsigned count;
};

class renderer_c {
public:
    renderer_c();

    // Compare `bank` (BANK_BYTES of video memory) against the previous pass
    // and repaint what changed. Returns the runs of screen lines that were
    // repainted; empty when the picture is unchanged.
    const std::vector<span_t> &update(const uint8_t *bank, const state_t &st);

    // One byte per pixel, 0 or 1, XSIZE * YSIZE, row major.
    const uint8_t *pixels() const { return pixels_.data(); }

    // Repaint everything on the next pass, for a freshly mapped window.
    void invalidate_all();

private:
    void read_scanline_map(const uint8_t *bank, uint16_t *out) const;
    void read_cursor(const uint8_t *bank, uint8_t *out) const;
    void paint_line(const uint8_t *bank, unsigned screen_line);
    void composite_cursor(unsigned screen_line);
    void mark(unsigned screen_line);

    std::vector<uint8_t> pixels_;       // XSIZE * YSIZE
    std::vector<uint8_t> staging_;      // this pass's copy of the bank
    std::vector<uint8_t> shadow_;       // previous pass's copy
    std::vector<uint16_t> map_;         // previous scanline map, YSIZE entries
    std::vector<uint8_t> cursor_;       // previous cursor image, 256 bits as bytes
    std::vector<uint8_t> dirty_;        // per screen line
    std::vector<span_t> spans_;

    state_t prev_state_;
    bool have_shadow_ = false;
};

}       // namespace vcb01

#endif
