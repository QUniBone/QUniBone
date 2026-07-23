/* vcb01_input.hpp: the VCB01's keyboard and pointer, behind a 2681 DUART

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

   The board carries an SCN2681 DUART with the LK201 keyboard on channel A and
   a VSXXX pointer on channel B. Both are serial: keystrokes and mouse reports
   arrive as received bytes, and the host's keyboard commands are transmitted
   bytes the keyboard answers. The register model follows the 2681 and the SIMH
   vax_2681 module; the keyboard and pointer protocols follow the LK201 and
   VSXXX-AA, after SIMH's vax_lk and vax_vs.

   This class holds no Xlib: it takes key and pointer events as plain values,
   so it can be exercised without a display.
*/
#ifndef _VCB01_INPUT_HPP_
#define _VCB01_INPUT_HPP_

#include <stdint.h>
#include <deque>

namespace vcb01 {

class input_c {
public:
    input_c();

    void reset(void);

    // --- the DUART, registers 0..15 as the board sees them ---
    // The register file is mirrored: duart_peek gives what a read returns
    // without disturbing anything, so the value is ready before the bus cycle
    // completes; duart_consume then pops a receive FIFO after the read.
    uint16_t duart_peek(unsigned reg) const;
    void duart_consume(unsigned reg);
    void duart_write(unsigned reg, uint16_t data);

    // --- events from the window ---
    // `keysym` is an X keysym; down is press vs release. Unmapped keys are
    // ignored.
    void key_event(unsigned keysym, bool down);
    // Relative pointer motion and the three buttons, left bit first.
    void pointer_event(int dx, int dy, bool left, bool middle, bool right);

    // The DUART raises an interrupt while an enabled source is pending. The
    // owner ORs this into the interrupt controller's DUART source.
    bool interrupt_pending(void) const;

    // Pointer buttons for the CSR, which reads them as one when up.
    bool button_left(void) const { return btn_left; }
    bool button_middle(void) const { return btn_middle; }
    bool button_right(void) const { return btn_right; }

private:
    // 2681 status bits (per channel)
    static const uint16_t STS_RXR = 0x0001;     // receiver ready
    static const uint16_t STS_TXR = 0x0004;     // transmitter ready
    static const uint16_t STS_TXE = 0x0008;     // transmitter empty
    // 2681 interrupt status bits
    static const uint16_t ISTS_TAI = 0x0001;    // transmitter ready A
    static const uint16_t ISTS_RAI = 0x0002;    // receiver ready A
    static const uint16_t ISTS_TBI = 0x0010;    // transmitter ready B
    static const uint16_t ISTS_RBI = 0x0020;    // receiver ready B
    // 2681 command bits
    static const uint16_t CMD_ERX = 0x0001;     // enable receiver
    static const uint16_t CMD_DRX = 0x0002;     // disable receiver
    static const uint16_t CMD_ETX = 0x0004;     // enable transmitter
    static const uint16_t CMD_DTX = 0x0008;     // disable transmitter

    struct port_t {
        uint16_t sts = 0;
        uint16_t cmd = 0;
        uint16_t mode[2] = { 0, 0 };
        unsigned mode_ptr = 0;
        std::deque<uint8_t> rxfifo;
    };
    port_t a, b;                // A = keyboard, B = pointer
    uint16_t ists = 0, imask = 0;

    void recompute_status(void);
    void queue_a(uint8_t c);    // deliver a byte to the keyboard channel
    void queue_b(uint8_t c);    // deliver a byte to the pointer channel

    // --- LK201 keyboard ---
    unsigned keys_down;
    void keyboard_command(uint8_t c);

    // --- VSXXX pointer ---
    bool btn_left, btn_middle, btn_right;
    bool prompt_mode;           // report only when asked, vs on every change
    void send_pointer_report(int dx, int dy);
};

}       // namespace vcb01

#endif
