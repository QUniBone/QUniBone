/* x11display.cpp: a monochrome bitmap window on an X server

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

#include <stdlib.h>
#include <string.h>
#include <netdb.h>
#include <sys/socket.h>
#include <arpa/inet.h>

#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/keysym.h>

#include "x11display.hpp"

// ---------------------------------------------------------------- name resolution

// Split "[proto/]host:display[.screen]". A bracketed IPv6 literal keeps its
// brackets, and the display suffix is found after the closing one.
static bool split_display_name(const std::string &spec, std::string *prefix,
        std::string *host, std::string *suffix)
{
    std::string rest = spec;

    size_t slash = rest.find('/');
    if (slash != std::string::npos) {
        *prefix = rest.substr(0, slash + 1);
        rest = rest.substr(slash + 1);
    } else
        prefix->clear();

    size_t colon;
    if (!rest.empty() && rest[0] == '[') {
        size_t close = rest.find(']');
        if (close == std::string::npos)
            return false;
        colon = rest.find(':', close);
    } else
        colon = rest.rfind(':');

    if (colon == std::string::npos)
        return false;

    *host = rest.substr(0, colon);
    *suffix = rest.substr(colon);
    return true;
}

static bool is_literal_address(const std::string &host)
{
    std::string bare = host;
    if (bare.size() >= 2 && bare.front() == '[' && bare.back() == ']')
        bare = bare.substr(1, bare.size() - 2);

    unsigned char buf[16];
    return inet_pton(AF_INET, bare.c_str(), buf) == 1
            || inet_pton(AF_INET6, bare.c_str(), buf) == 1;
}

bool x11_resolve_display_name(const std::string &spec, std::string *resolved,
        std::string *error)
{
    error->clear();

    std::string in = spec;
    if (in.empty()) {
        const char *env = getenv("DISPLAY");
        in = env ? env : "";
    }
    if (in.empty()) {
        *error = "no display given and DISPLAY is not set";
        return false;
    }

    // A name beginning with "/" is a path to a local socket - what macOS
    // hands out through launchd - and has no host part to look up.
    if (in[0] == '/') {
        *resolved = in;
        return true;
    }

    std::string prefix, host, suffix;
    if (!split_display_name(in, &prefix, &host, &suffix)) {
        // Not of the form host:number - hand it to Xlib as it stands and let
        // it produce the complaint.
        *resolved = in;
        return true;
    }

    // A local transport and an address that needs no lookup both pass through.
    if (host.empty() || host == "unix" || host == "localhost"
            || is_literal_address(host)) {
        *resolved = in;
        return true;
    }

    struct addrinfo hints;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;

    struct addrinfo *res = nullptr;
    int rc = getaddrinfo(host.c_str(), nullptr, &hints, &res);
    if (rc != 0 || res == nullptr) {
        *error = "cannot resolve \"" + host + "\": " + gai_strerror(rc);
        return false;
    }

    // Prefer IPv4: an X display name carries an IPv6 literal only in bracket
    // form, which not every server side parses.
    const struct addrinfo *chosen = nullptr;
    for (const struct addrinfo *a = res; a != nullptr; a = a->ai_next)
        if (a->ai_family == AF_INET) {
            chosen = a;
            break;
        }
    if (chosen == nullptr)
        chosen = res;

    char text[INET6_ADDRSTRLEN];
    std::string literal;
    if (chosen->ai_family == AF_INET) {
        const struct sockaddr_in *sa = (const struct sockaddr_in *) chosen->ai_addr;
        inet_ntop(AF_INET, &sa->sin_addr, text, sizeof(text));
        literal = text;
    } else {
        const struct sockaddr_in6 *sa = (const struct sockaddr_in6 *) chosen->ai_addr;
        inet_ntop(AF_INET6, &sa->sin6_addr, text, sizeof(text));
        literal = "[" + std::string(text) + "]";
    }
    freeaddrinfo(res);

    *resolved = prefix + literal + suffix;
    return true;
}

// ---------------------------------------------------------------- the window

struct x11display_c::impl_t {
    Display *dpy = nullptr;
    Window win = 0;
    GC gc = 0;
    XImage *image = nullptr;
    Atom wm_delete = 0;
    uint32_t palette[2] = { 0, 0 };
    std::vector<uint8_t> buffer;        // backing store for `image`
    unsigned bytes_per_line = 0;
    unsigned bytes_per_pixel = 0;
    int last_x = -1, last_y = -1;
};

x11display_c::x11display_c()
{
    impl_ = new impl_t();
}

x11display_c::~x11display_c()
{
    close();
    delete impl_;
}

bool x11display_c::open(const std::string &display, const std::string &title,
        unsigned width, unsigned height)
{
    close();
    error_.clear();

    if (!x11_resolve_display_name(display, &resolved_, &error_))
        return false;

    Display *dpy = XOpenDisplay(resolved_.c_str());
    if (dpy == nullptr) {
        error_ = "cannot open display \"" + resolved_ + "\"";
        return false;
    }

    int screen = DefaultScreen(dpy);
    Visual *visual = DefaultVisual(dpy, screen);
    int depth = DefaultDepth(dpy, screen);

    impl_->dpy = dpy;
    width_ = width;
    height_ = height;

    impl_->win = XCreateSimpleWindow(dpy, RootWindow(dpy, screen), 0, 0,
            width, height, 0, BlackPixel(dpy, screen), BlackPixel(dpy, screen));

    XStoreName(dpy, impl_->win, title.c_str());

    // The board's own size is the only size that makes sense, so ask the
    // window manager not to resize it.
    XSizeHints *hints = XAllocSizeHints();
    if (hints != nullptr) {
        hints->flags = PMinSize | PMaxSize;
        hints->min_width = hints->max_width = (int) width;
        hints->min_height = hints->max_height = (int) height;
        XSetWMNormalHints(dpy, impl_->win, hints);
        XFree(hints);
    }

    XSelectInput(dpy, impl_->win, ExposureMask | KeyPressMask | KeyReleaseMask
            | ButtonPressMask | ButtonReleaseMask | PointerMotionMask
            | StructureNotifyMask);

    impl_->wm_delete = XInternAtom(dpy, "WM_DELETE_WINDOW", False);
    XSetWMProtocols(dpy, impl_->win, &impl_->wm_delete, 1);

    impl_->gc = XCreateGC(dpy, impl_->win, 0, nullptr);
    XMapWindow(dpy, impl_->win);

    // An image the full width of the screen and as tall as the screen, so any
    // run of rows can be sent from one buffer.
    XImage *probe = XCreateImage(dpy, visual, depth, ZPixmap, 0, nullptr,
            width, height, 32, 0);
    if (probe == nullptr) {
        error_ = "XCreateImage failed";
        close();
        return false;
    }
    impl_->bytes_per_line = probe->bytes_per_line;
    impl_->bytes_per_pixel = (unsigned) (probe->bits_per_pixel / 8);
    impl_->buffer.assign((size_t) impl_->bytes_per_line * height, 0);
    probe->data = (char *) impl_->buffer.data();
    impl_->image = probe;

    // Green on black, in the visual's own channel positions.
    unsigned long rmask = visual->red_mask, gmask = visual->green_mask,
            bmask = visual->blue_mask;
    impl_->palette[0] = 0;
    impl_->palette[1] = (uint32_t) ((rmask & 0x33333333ul) | gmask | (bmask & 0x33333333ul));
    if (rmask == 0 && gmask == 0 && bmask == 0)
        impl_->palette[1] = (uint32_t) WhitePixel(dpy, screen);

    display_ = dpy;
    return true;
}

void x11display_c::close()
{
    if (impl_ == nullptr || impl_->dpy == nullptr) {
        display_ = nullptr;
        return;
    }
    set_pointer_grab(false);
    if (impl_->image != nullptr) {
        // The buffer belongs to this object, not to Xlib.
        impl_->image->data = nullptr;
        XDestroyImage(impl_->image);
        impl_->image = nullptr;
    }
    if (impl_->gc != 0) {
        XFreeGC(impl_->dpy, impl_->gc);
        impl_->gc = 0;
    }
    if (impl_->win != 0) {
        XDestroyWindow(impl_->dpy, impl_->win);
        impl_->win = 0;
    }
    XCloseDisplay(impl_->dpy);
    impl_->dpy = nullptr;
    display_ = nullptr;
}

void x11display_c::put_rows(const uint8_t *pixels, unsigned first, unsigned count)
{
    if (display_ == nullptr || count == 0 || first >= height_)
        return;
    if (first + count > height_)
        count = height_ - first;

    const uint32_t on = impl_->palette[1];
    const uint32_t off = impl_->palette[0];

    for (unsigned row = 0; row < count; row++) {
        const uint8_t *src = pixels + (size_t) (first + row) * width_;
        uint8_t *dst_row = impl_->buffer.data()
                + (size_t) (first + row) * impl_->bytes_per_line;

        if (impl_->bytes_per_pixel == 4) {
            uint32_t *dst = (uint32_t *) dst_row;
            for (unsigned x = 0; x < width_; x++)
                dst[x] = src[x] ? on : off;
        } else {
            for (unsigned x = 0; x < width_; x++)
                XPutPixel(impl_->image, (int) x, (int) (first + row),
                        src[x] ? on : off);
        }
    }

    XPutImage(impl_->dpy, impl_->win, impl_->gc, impl_->image,
            0, (int) first, 0, (int) first, width_, count);
}

void x11display_c::flush()
{
    if (display_ != nullptr)
        XFlush(impl_->dpy);
}

void x11display_c::warp_to_centre()
{
    XWarpPointer(impl_->dpy, None, impl_->win, 0, 0, 0, 0,
            (int) (width_ / 2), (int) (height_ / 2));
    impl_->last_x = (int) (width_ / 2);
    impl_->last_y = (int) (height_ / 2);
}

void x11display_c::set_pointer_grab(bool on)
{
    if (display_ == nullptr || on == grabbed_)
        return;
    if (on) {
        XGrabPointer(impl_->dpy, impl_->win, True,
                ButtonPressMask | ButtonReleaseMask | PointerMotionMask,
                GrabModeAsync, GrabModeAsync, impl_->win, None, CurrentTime);
        XGrabKeyboard(impl_->dpy, impl_->win, True, GrabModeAsync, GrabModeAsync,
                CurrentTime);
        warp_to_centre();
    } else {
        XUngrabPointer(impl_->dpy, CurrentTime);
        XUngrabKeyboard(impl_->dpy, CurrentTime);
        impl_->last_x = impl_->last_y = -1;
    }
    grabbed_ = on;
}

const std::vector<x11display_c::event_t> &x11display_c::poll_events()
{
    events_.clear();
    if (display_ == nullptr)
        return events_;

    while (XPending(impl_->dpy)) {
        XEvent xe;
        XNextEvent(impl_->dpy, &xe);
        event_t ev;

        switch (xe.type) {
        case Expose:
            ev.kind = event_t::EXPOSED;
            events_.push_back(ev);
            break;

        case KeyPress:
        case KeyRelease:
            ev.kind = (xe.type == KeyPress) ? event_t::KEY_PRESS : event_t::KEY_RELEASE;
            ev.keycode = xe.xkey.keycode;
            ev.keysym = (unsigned) XLookupKeysym(&xe.xkey, 0);
            events_.push_back(ev);
            break;

        case ButtonPress:
        case ButtonRelease:
            ev.kind = (xe.type == ButtonPress) ? event_t::BUTTON_PRESS
                                               : event_t::BUTTON_RELEASE;
            ev.button = xe.xbutton.button;
            ev.x = xe.xbutton.x;
            ev.y = xe.xbutton.y;
            events_.push_back(ev);
            break;

        case MotionNotify: {
            ev.kind = event_t::MOTION;
            ev.x = xe.xmotion.x;
            ev.y = xe.xmotion.y;
            if (impl_->last_x >= 0) {
                ev.dx = xe.xmotion.x - impl_->last_x;
                ev.dy = xe.xmotion.y - impl_->last_y;
            }
            impl_->last_x = xe.xmotion.x;
            impl_->last_y = xe.xmotion.y;
            // A grabbed pointer reports movement, so it is pushed back to the
            // middle once it has drifted far enough to risk leaving the window.
            if (grabbed_ && (ev.dx != 0 || ev.dy != 0)) {
                int cx = (int) (width_ / 2), cy = (int) (height_ / 2);
                if (abs(xe.xmotion.x - cx) > (int) width_ / 4
                        || abs(xe.xmotion.y - cy) > (int) height_ / 4)
                    warp_to_centre();
            }
            if (ev.dx != 0 || ev.dy != 0)
                events_.push_back(ev);
            break;
        }

        case ClientMessage:
            if ((Atom) xe.xclient.data.l[0] == impl_->wm_delete) {
                ev.kind = event_t::CLOSED;
                events_.push_back(ev);
            }
            break;

        default:
            break;
        }
    }
    return events_;
}
