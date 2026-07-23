/* vcb01_render.cpp: QVSS (VCB01) video memory to pixels

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
*/

#include <string.h>

#include "vcb01_render.hpp"

namespace vcb01 {

renderer_c::renderer_c()
{
    pixels_.assign(XSIZE * YSIZE, 0);
    staging_.assign(BANK_BYTES, 0);
    shadow_.assign(BANK_BYTES, 0);
    map_.assign(YSIZE, 0);
    cursor_.assign(CURSOR_SIZE * CURSOR_SIZE, 0);
    dirty_.assign(YSIZE, 0);
    spans_.reserve(64);
}

void renderer_c::invalidate_all()
{
    have_shadow_ = false;
}

void renderer_c::mark(unsigned screen_line)
{
    if (screen_line < YSIZE)
        dirty_[screen_line] = 1;
}

// The map holds 11-bit entries packed two to a longword, the even screen line
// in the low half.
void renderer_c::read_scanline_map(const uint8_t *bank, uint16_t *out) const
{
    const uint8_t *map = bank + MAP_LW * 4;
    for (unsigned line = 0; line < YSIZE; line++) {
        // longword (line / 2), half (line & 1), little endian within the bank
        const uint8_t *p = map + (line / 2) * 4 + (line & 1) * 2;
        out[line] = (uint16_t) ((p[0] | (p[1] << 8)) & 0x7FF);
    }
}

// 256 bits, least significant bit of each byte first, expanded to one byte
// per cursor pixel so compositing does not have to shift.
void renderer_c::read_cursor(const uint8_t *bank, uint8_t *out) const
{
    const uint8_t *cur = bank + CURSOR_LW * 4;
    for (unsigned i = 0; i < CURSOR_SIZE * CURSOR_SIZE; i++)
        out[i] = (cur[i / 8] >> (i % 8)) & 1;
}

// Expand one screen line from the buffer line the scanline map names for it.
void renderer_c::paint_line(const uint8_t *bank, unsigned screen_line)
{
    uint8_t *row = pixels_.data() + (size_t) screen_line * XSIZE;
    unsigned buffer_line = map_[screen_line];

    if (buffer_line >= BUFFER_LINES) {
        // A map entry pointing past the bitmap shows nothing rather than
        // reading the scanline map back as picture.
        memset(row, 0, XSIZE);
        return;
    }

    const uint8_t *src = bank + (size_t) buffer_line * LINE_BYTES;
    for (unsigned byte = 0; byte < LINE_BYTES; byte++) {
        uint8_t bits = src[byte];
        uint8_t *out = row + byte * 8;
        out[0] = bits & 1;
        out[1] = (bits >> 1) & 1;
        out[2] = (bits >> 2) & 1;
        out[3] = (bits >> 3) & 1;
        out[4] = (bits >> 4) & 1;
        out[5] = (bits >> 5) & 1;
        out[6] = (bits >> 6) & 1;
        out[7] = (bits >> 7) & 1;
    }
}

// OR paints the cursor into the line, AND cuts it out.
void renderer_c::composite_cursor(unsigned screen_line)
{
    unsigned top = prev_state_.cursor_y;
    if (screen_line < top || screen_line >= top + CURSOR_SIZE)
        return;

    unsigned crow = screen_line - top;
    uint8_t *row = pixels_.data() + (size_t) screen_line * XSIZE;

    for (unsigned col = 0; col < CURSOR_SIZE; col++) {
        unsigned x = prev_state_.cursor_x + col;
        if (x >= XSIZE)
            break;
        uint8_t bit = cursor_[crow * CURSOR_SIZE + col];
        if (prev_state_.cursor_or)
            row[x] = row[x] | bit;
        else
            row[x] = row[x] & (uint8_t) !bit;
    }
}

const std::vector<span_t> &renderer_c::update(const uint8_t *bank, const state_t &st)
{
    bool full = !have_shadow_;
    memset(dirty_.data(), 0, dirty_.size());

    // Video memory is uncached on the way to the ARM, so it is read exactly
    // once per pass: everything below compares and paints out of this copy,
    // which the cache does hold.
    memcpy(staging_.data(), bank, BANK_BYTES);
    bank = staging_.data();

    // The map is read every pass: an entry that changed moves a whole screen
    // line to different pixels.
    std::vector<uint16_t> new_map(YSIZE);
    read_scanline_map(bank, new_map.data());
    for (unsigned line = 0; line < YSIZE; line++)
        if (full || new_map[line] != map_[line]) {
            map_[line] = new_map[line];
            mark(line);
        }

    // A buffer line that changed dirties every screen line showing it, so
    // collect the changed lines first and then walk the map once.
    if (!full) {
        std::vector<uint8_t> changed(BUFFER_LINES, 0);
        bool any = false;
        for (unsigned bl = 0; bl < BUFFER_LINES; bl++) {
            size_t off = (size_t) bl * LINE_BYTES;
            if (memcmp(bank + off, shadow_.data() + off, LINE_BYTES) != 0) {
                changed[bl] = 1;
                any = true;
            }
        }
        if (any)
            for (unsigned line = 0; line < YSIZE; line++)
                if (map_[line] < BUFFER_LINES && changed[map_[line]])
                    mark(line);
    }

    // Cursor image, position, visibility and function each move pixels; the
    // rows it used to occupy have to be repainted as well as the new ones.
    uint8_t new_cursor[CURSOR_SIZE * CURSOR_SIZE];
    read_cursor(bank, new_cursor);
    bool cursor_moved = full
            || st.cursor_x != prev_state_.cursor_x
            || st.cursor_y != prev_state_.cursor_y
            || st.cursor_visible != prev_state_.cursor_visible
            || st.cursor_or != prev_state_.cursor_or
            || memcmp(new_cursor, cursor_.data(), sizeof(new_cursor)) != 0;

    if (cursor_moved && !full) {
        if (prev_state_.cursor_visible)
            for (unsigned i = 0; i < CURSOR_SIZE; i++)
                mark(prev_state_.cursor_y + i);
        if (st.cursor_visible)
            for (unsigned i = 0; i < CURSOR_SIZE; i++)
                mark(st.cursor_y + i);
    }

    // Enabling or disabling video repaints the whole screen: the bitmap is
    // unchanged across the transition, so without this nothing would be marked
    // dirty and the screen would keep whatever it showed before.
    bool video_toggled = st.video_enable != prev_state_.video_enable;

    memcpy(cursor_.data(), new_cursor, sizeof(new_cursor));
    prev_state_ = st;

    if (full || video_toggled)
        memset(dirty_.data(), 1, dirty_.size());

    // Repaint what was marked. Video disabled blanks the screen without
    // disturbing the shadow, so re-enabling it brings the picture back.
    for (unsigned line = 0; line < YSIZE; line++) {
        if (!dirty_[line])
            continue;
        if (!st.video_enable)
            memset(pixels_.data() + (size_t) line * XSIZE, 0, XSIZE);
        else {
            paint_line(bank, line);
            if (st.cursor_visible)
                composite_cursor(line);
        }
    }

    // This pass's copy becomes the next pass's reference; the buffer it
    // replaces is reused for the next read.
    staging_.swap(shadow_);
    have_shadow_ = true;

    // Coalesce into runs so the caller issues one put per run.
    spans_.clear();
    unsigned line = 0;
    while (line < YSIZE) {
        if (!dirty_[line]) {
            line++;
            continue;
        }
        unsigned first = line;
        while (line < YSIZE && dirty_[line])
            line++;
        spans_.push_back(span_t { first, line - first });
    }
    return spans_;
}

}       // namespace vcb01
