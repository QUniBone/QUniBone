/* webvcb01.cpp: /ws/vcb01 — the VCB01 framebuffer over WebSocket

   Copyright (c) 2026, Hans Huebner

   The refresh worker calls webvcb01_publish() once per pass with the whole
   rendered image and the runs of screen lines that changed. This file keeps
   the set of connected browsers and sends each what it needs: a client that
   just attached gets one full frame, then only the changed spans.

   Wire format (server to client, binary frames), the width a multiple of 8:

     full frame  0x01  w:u16  h:u16  <h * w/8 bytes, 1 bpp, MSB leftmost>
     span        0x02  first:u16  count:u16  <count * w/8 bytes>

   A client draws a full frame into a canvas of the given size and patches the
   named line runs on every span. The width carried by the last full frame
   holds for the spans that follow.
*/

#include <mutex>
#include <set>
#include <string>
#include <vector>

#include "civetweb.h"
#include "webvcb01.hpp"

static std::mutex clients_mutex;
static std::set<struct mg_connection *> clients;
// Clients owed a full frame: a fresh connection, or everyone after a resize.
static std::set<struct mg_connection *> need_full;
static unsigned last_width = 0, last_height = 0;

bool webvcb01_watching(void) {
	std::lock_guard<std::mutex> lock(clients_mutex);
	return !clients.empty();
}

// Pack `count` rows starting at `first` from the one-byte-per-pixel image into
// 1 bpp, MSB leftmost, appended to `out`.
static void pack_rows(std::string &out, unsigned width, const unsigned char *pixels,
		unsigned first, unsigned count) {
	unsigned stride = width / 8;
	size_t base = out.size();
	out.resize(base + (size_t) count * stride, 0);
	for (unsigned r = 0; r < count; r++) {
		const unsigned char *src = pixels + (size_t)(first + r) * width;
		char *dst = &out[base + (size_t) r * stride];
		for (unsigned x = 0; x < width; x++)
			if (src[x])
				dst[x >> 3] |= (char) (0x80 >> (x & 7));
	}
}

void webvcb01_publish(unsigned width, unsigned height, const unsigned char *pixels,
		const std::vector<vcb01::span_t> &spans) {
	std::lock_guard<std::mutex> lock(clients_mutex);
	if (clients.empty())
		return;

	// A different screen size makes every client's canvas stale.
	if (width != last_width || height != last_height) {
		need_full = clients;
		last_width = width;
		last_height = height;
	}

	if (!need_full.empty()) {
		std::string msg;
		msg.push_back((char) 0x01);
		msg.push_back((char) (width >> 8));  msg.push_back((char) (width & 0xFF));
		msg.push_back((char) (height >> 8)); msg.push_back((char) (height & 0xFF));
		pack_rows(msg, width, pixels, 0, height);
		for (struct mg_connection *conn : need_full)
			mg_websocket_write(conn, MG_WEBSOCKET_OPCODE_BINARY, msg.data(), msg.size());
	}

	// Clients that just got a full frame are current; the rest get the spans.
	std::set<struct mg_connection *> established = clients;
	for (struct mg_connection *conn : need_full)
		established.erase(conn);
	need_full.clear();

	if (established.empty())
		return;
	for (const vcb01::span_t &s : spans) {
		std::string msg;
		msg.push_back((char) 0x02);
		msg.push_back((char) (s.first >> 8)); msg.push_back((char) (s.first & 0xFF));
		msg.push_back((char) (s.count >> 8)); msg.push_back((char) (s.count & 0xFF));
		pack_rows(msg, width, pixels, s.first, s.count);
		for (struct mg_connection *conn : established)
			mg_websocket_write(conn, MG_WEBSOCKET_OPCODE_BINARY, msg.data(), msg.size());
	}
}

static int ws_connect_handler(const struct mg_connection *, void *) {
	return 0; // accept even when the board is not drawing
}

static void ws_ready_handler(struct mg_connection *conn, void *) {
	std::lock_guard<std::mutex> lock(clients_mutex);
	clients.insert(conn);
	need_full.insert(conn); // owed a full frame on the next pass
}

static int ws_data_handler(struct mg_connection *, int opcode, char *, size_t, void *) {
	// A display only: nothing from the client but the close.
	if ((opcode & 0x0f) == MG_WEBSOCKET_OPCODE_CONNECTION_CLOSE)
		return 0;
	return 1;
}

static void ws_close_handler(const struct mg_connection *conn, void *) {
	std::lock_guard<std::mutex> lock(clients_mutex);
	clients.erase(const_cast<struct mg_connection *>(conn));
	need_full.erase(const_cast<struct mg_connection *>(conn));
}

void webvcb01_register(struct mg_context *ctx) {
	mg_set_websocket_handler(ctx, "/ws/vcb01", ws_connect_handler,
			ws_ready_handler, ws_data_handler, ws_close_handler, nullptr);
}

void webvcb01_shutdown(void) {
	std::lock_guard<std::mutex> lock(clients_mutex);
	clients.clear();
	need_full.clear();
}
