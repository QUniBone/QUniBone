/* x11display.hpp: a monochrome bitmap window on an X server

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

   Carries a one-byte-per-pixel monochrome image to an X server, and hands
   back the keyboard and pointer events the window receives.

   The emulator runs headless and reaches a server elsewhere on the network,
   so the display name is resolved here rather than in Xlib: a host name
   becomes a literal address before XOpenDisplay() sees it.
*/
#ifndef _X11DISPLAY_HPP_
#define _X11DISPLAY_HPP_

#include <stdint.h>
#include <string>
#include <vector>

// Resolve the host part of an X display name to a literal address, leaving
// the transport prefix and the ":display.screen" suffix alone. Names that
// need no lookup - empty, "unix", "localhost", an address already - come back
// unchanged. Returns false and fills `error` when the host does not resolve.
//
// Exposed for its own sake so it can be tested without an X server.
bool x11_resolve_display_name(const std::string &spec, std::string *resolved,
        std::string *error);

class x11display_c {
public:
    // What the window reports back. Positions are in pixels within the image.
    struct event_t {
        enum kind_t { KEY_PRESS, KEY_RELEASE, BUTTON_PRESS, BUTTON_RELEASE,
                      MOTION, CLOSED, EXPOSED } kind;
        unsigned keycode = 0;       // X keycode, for KEY_*
        unsigned keysym = 0;        // X keysym, for KEY_*
        unsigned button = 0;        // 1..5, for BUTTON_*
        int x = 0, y = 0;           // for MOTION and BUTTON_*
        int dx = 0, dy = 0;         // pointer movement since the last event
    };

    x11display_c();
    ~x11display_c();

    // `display` is an X display name, `title` the window title. Returns false
    // with a reason in error() when the server cannot be reached.
    bool open(const std::string &display, const std::string &title,
            unsigned width, unsigned height);
    void close();
    bool is_open() const { return display_ != nullptr; }

    // Change the drawable area, e.g. when the CRTC sets a different height.
    // A no-op if the size is unchanged. Xlib is single-threaded, so call this
    // from the same thread that draws.
    void resize(unsigned width, unsigned height);

    const std::string &error() const { return error_; }
    // The display name actually handed to Xlib, after resolution.
    const std::string &resolved_display() const { return resolved_; }

    // Copy `count` rows starting at `first` out of a width*height image of
    // one byte per pixel, and send them to the server.
    void put_rows(const uint8_t *pixels, unsigned first, unsigned count);
    void flush();

    // Drain everything the server has sent. An EXPOSED event means the caller
    // has to repaint in full.
    const std::vector<event_t> &poll_events();

    // Hold the pointer inside the window and report relative movement, which
    // is what a mouse speaking an incremental protocol needs.
    void set_pointer_grab(bool on);
    bool pointer_grabbed() const { return grabbed_; }

private:
    void warp_to_centre();

    struct impl_t;
    impl_t *impl_ = nullptr;        // keeps Xlib out of this header

    void *display_ = nullptr;       // Display*, non-null while open
    std::string error_;
    std::string resolved_;
    unsigned width_ = 0, height_ = 0;
    bool grabbed_ = false;
    std::vector<event_t> events_;
};

#endif
