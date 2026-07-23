/* vcb01_input.cpp: the VCB01's keyboard and pointer, behind a 2681 DUART

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

#include "vcb01_input.hpp"

namespace vcb01 {

// ---------------------------------------------------------------- LK201 codes

// Special LK201 bytes.
static const uint8_t LK_ALLUP = 0xB3;       // every key released
static const uint8_t LK_MODEACK = 0xBA;     // a command was accepted

// An X keysym mapped to its LK201 make code, or 0 for keys the board has no
// code for. Only the keysyms a driver needs to see typing are covered; the
// numeric keypad and the top function keys can be added the same way.
static uint8_t lk_code(unsigned keysym)
{
    // Letters: X gives the lowercase keysym for an unmodified letter key.
    static const uint8_t letter[26] = {
        /* a */ 0xC2, 0xD9, 0xCE, 0xCD, 0xCC, 0xD2, 0xD8, 0xDD, 0xE6, 0xE2,
        /* k */ 0xE7, 0xEC, 0xE3, 0xDE, 0xEB, 0xF0, 0xC1, 0xD1, 0xC7, 0xD7,
        /* u */ 0xE1, 0xD3, 0xC6, 0xC8, 0xDC, 0xC3 };
    static const uint8_t digit[10] = {      // top-row 0..9
        0xEF, 0xC0, 0xC5, 0xCB, 0xD0, 0xD6, 0xDB, 0xE0, 0xE5, 0xEA };

    if (keysym >= 'a' && keysym <= 'z')
        return letter[keysym - 'a'];
    if (keysym >= 'A' && keysym <= 'Z')     // a captured shift may deliver upper
        return letter[keysym - 'A'];
    if (keysym >= '0' && keysym <= '9')
        return digit[keysym - '0'];

    switch (keysym) {
    case ' ':       return 0xD4;            // space
    case 0xFF0D:    return 0xBD;            // Return
    case 0xFF09:    return 0xBE;            // Tab
    case 0xFF08:    return 0xBC;            // BackSpace -> Delete
    case 0xFFFF:    return 0x8C;            // Delete -> Remove
    case ';':       return 0xF2;
    case '=':       return 0xF5;
    case ',':       return 0xE8;
    case '-':       return 0xF9;
    case '.':       return 0xED;
    case '/':       return 0xF3;
    case '\'':      return 0xFB;
    case '[':       return 0xFA;
    case ']':       return 0xF6;
    case '\\':      return 0xF7;
    case '`':       return 0xBF;
    case 0xFFE1:    return 0xAE;            // Shift_L
    case 0xFFE2:    return 0xAE;            // Shift_R
    case 0xFFE3:    return 0xAF;            // Control_L
    case 0xFFE4:    return 0xAF;            // Control_R
    case 0xFFE5:    return 0xB0;            // Caps_Lock -> Lock
    case 0xFF51:    return 0xA7;            // Left
    case 0xFF52:    return 0xAA;            // Up
    case 0xFF53:    return 0xA8;            // Right
    case 0xFF54:    return 0xA9;            // Down
    default:        return 0;
    }
}

// ---------------------------------------------------------------- DUART

input_c::input_c()
{
    reset();
}

void input_c::reset(void)
{
    a = port_t();
    b = port_t();
    ists = 0;
    imask = 0;
    keys_down = 0;
    btn_left = btn_middle = btn_right = false;
    prompt_mode = false;
    recompute_status();
}

// A DUART transmitter is ready whenever the host has enabled it: the keyboard
// and pointer accept bytes as fast as they arrive. A receiver is ready while
// its FIFO holds a byte. The interrupt-status bits track the same conditions.
void input_c::recompute_status(void)
{
    a.sts = (a.rxfifo.empty() ? 0 : STS_RXR)
            | ((a.cmd & CMD_ETX) ? (STS_TXR | STS_TXE) : 0);
    b.sts = (b.rxfifo.empty() ? 0 : STS_RXR)
            | ((b.cmd & CMD_ETX) ? (STS_TXR | STS_TXE) : 0);

    ists = 0;
    if (!a.rxfifo.empty()) ists |= ISTS_RAI;
    if (!b.rxfifo.empty()) ists |= ISTS_RBI;
    if (a.cmd & CMD_ETX)   ists |= ISTS_TAI;
    if (b.cmd & CMD_ETX)   ists |= ISTS_TBI;
}

bool input_c::interrupt_pending(void) const
{
    return (ists & imask) != 0;
}

void input_c::queue_a(uint8_t c)
{
    a.rxfifo.push_back(c);
    recompute_status();
}

void input_c::queue_b(uint8_t c)
{
    b.rxfifo.push_back(c);
    recompute_status();
}

uint16_t input_c::duart_peek(unsigned reg) const
{
    switch (reg) {
    case 0:     // mode 1A / 2A
        return a.mode[a.mode_ptr];
    case 1:     // status A
        return a.sts;
    case 3:     // rx buffer A, with status in the high byte
        return (uint16_t) ((a.rxfifo.empty() ? 0 : a.rxfifo.front())
                | (a.sts << 8));
    case 5:     // interrupt status
        return ists;
    case 8:     // mode 1B / 2B
        return b.mode[b.mode_ptr];
    case 9:     // status B
        return b.sts;
    case 11:    // rx buffer B
        return (uint16_t) ((b.rxfifo.empty() ? 0 : b.rxfifo.front())
                | (b.sts << 8));
    default:
        return 0;
    }
}

// A read of a receive buffer pops its byte and advances the mode pointer for a
// mode register; the value was already delivered by duart_peek.
void input_c::duart_consume(unsigned reg)
{
    switch (reg) {
    case 0:
        a.mode_ptr = (a.mode_ptr + 1) & 1;
        break;
    case 3:
        if (!a.rxfifo.empty())
            a.rxfifo.pop_front();
        recompute_status();
        break;
    case 8:
        b.mode_ptr = (b.mode_ptr + 1) & 1;
        break;
    case 11:
        if (!b.rxfifo.empty())
            b.rxfifo.pop_front();
        recompute_status();
        break;
    default:
        break;
    }
}

void input_c::duart_write(unsigned reg, uint16_t data)
{
    uint8_t d = (uint8_t) (data & 0xFF);
    switch (reg) {
    case 0:     // mode 1A / 2A
        a.mode[a.mode_ptr] = d;
        a.mode_ptr = (a.mode_ptr + 1) & 1;
        break;
    case 2:     // command A
        if (d & CMD_ETX) a.cmd |= CMD_ETX;
        else if (d & CMD_DTX) a.cmd &= ~CMD_ETX;
        if (d & CMD_ERX) a.cmd |= CMD_ERX;
        else if (d & CMD_DRX) a.cmd &= ~CMD_ERX;
        if (((d >> 4) & 7) == 1)        // reset mode pointer
            a.mode_ptr = 0;
        recompute_status();
        break;
    case 3:     // tx buffer A: a byte to the keyboard
        keyboard_command(d);
        break;
    case 5:     // interrupt mask
        imask = d;
        break;
    case 8:     // mode 1B / 2B
        b.mode[b.mode_ptr] = d;
        b.mode_ptr = (b.mode_ptr + 1) & 1;
        break;
    case 10:    // command B
        if (d & CMD_ETX) b.cmd |= CMD_ETX;
        else if (d & CMD_DTX) b.cmd &= ~CMD_ETX;
        if (d & CMD_ERX) b.cmd |= CMD_ERX;
        else if (d & CMD_DRX) b.cmd &= ~CMD_ERX;
        if (((d >> 4) & 7) == 1)
            b.mode_ptr = 0;
        recompute_status();
        break;
    case 11:    // tx buffer B: a byte to the pointer
        // The VSXXX answers a prompt request ("R", 0x52) with one report, and
        // "D" (0x44) puts it in prompt mode - report only when asked.
        if (d == 0x52)
            send_pointer_report(0, 0);
        else if (d == 0x44)
            prompt_mode = true;
        break;
    default:
        break;
    }
}

// ---------------------------------------------------------------- keyboard

// The host talks to the keyboard by transmitting command bytes. The ones a
// driver waits on for an answer are handled; the rest are accepted silently.
void input_c::keyboard_command(uint8_t c)
{
    if (c & 1) {                        // a peripheral command
        switch (c) {
        case 0xAB:                      // request keyboard ID
            queue_a(0x01);
            queue_a(0x00);
            break;
        case 0xFD:                      // jump to power-up: self-test result
            queue_a(0x01);
            queue_a(0x00);
            queue_a(0x00);
            queue_a(0x00);
            break;
        case 0xD3:                      // reinstate defaults
            keys_down = 0;
            queue_a(LK_MODEACK);
            break;
        default:                        // LEDs, bell, keyclick, auto-repeat...
            break;
        }
    } else {                            // set a key group's mode
        if (((c >> 3) & 0xF) < 15)
            queue_a(LK_MODEACK);
    }
}

void input_c::key_event(unsigned keysym, bool down)
{
    uint8_t code = lk_code(keysym);
    if (code == 0)
        return;

    if (down) {
        keys_down++;
        queue_a(code);
    } else {
        if (keys_down > 0)
            keys_down--;
        // The board reports the individual key only for the up/down groups;
        // for the common typing keys the release is announced by the all-up
        // code once the last key is gone.
        if (keys_down == 0)
            queue_a(LK_ALLUP);
    }
}

// ---------------------------------------------------------------- pointer

// A VSXXX report is three bytes: a header carrying the buttons and the sign of
// each axis, then the magnitudes. Movement is clamped to a byte per axis.
void input_c::send_pointer_report(int dx, int dy)
{
    if (dx > 127) dx = 127; else if (dx < -127) dx = -127;
    if (dy > 127) dy = 127; else if (dy < -127) dy = -127;

    uint8_t header = 0x80;
    if (btn_right)  header |= 0x01;
    if (btn_middle) header |= 0x02;
    if (btn_left)   header |= 0x04;
    if (dx >= 0)    header |= 0x10;     // sign of X
    if (dy < 0)     header |= 0x08;     // sign of Y (screen Y grows downward)

    queue_b(header);
    queue_b((uint8_t) (dx < 0 ? -dx : dx));
    queue_b((uint8_t) (dy < 0 ? -dy : dy));
}

void input_c::pointer_event(int dx, int dy, bool left, bool middle, bool right)
{
    btn_left = left;
    btn_middle = middle;
    btn_right = right;
    if (!prompt_mode)
        send_pointer_report(dx, dy);
}

}       // namespace vcb01
