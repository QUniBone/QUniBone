/* webvcb01.hpp: /ws/vcb01 — the VCB01 framebuffer over WebSocket

   Copyright (c) 2026, Hans Huebner

   A display path for the VCB01 that needs no X server: the refresh worker
   hands each rendered frame here, and every connected browser gets it. A
   client that has just attached (or one whose screen changed size) receives a
   full frame; after that it receives only the screen-line runs that changed.
*/
#ifndef _WEBVCB01_HPP_
#define _WEBVCB01_HPP_

#include <vector>
#include "vcb01_render.hpp"     // vcb01::span_t

struct mg_context;

void webvcb01_register(struct mg_context *ctx);
void webvcb01_shutdown(void);

// True while a browser is watching, so the refresh worker renders even when no
// X display is open.
bool webvcb01_watching(void);

// Hand the worker's rendered frame to the web clients. `pixels` is
// width*height bytes, one per pixel (0 or 1), row major; `spans` are the
// screen-line runs repainted this pass. New clients get a full frame, the rest
// get the spans.
void webvcb01_publish(unsigned width, unsigned height, const unsigned char *pixels,
                      const std::vector<vcb01::span_t> &spans);

#endif
