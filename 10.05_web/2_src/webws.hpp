/* webws.hpp: non-blocking WebSocket broadcast helper

   Copyright (c) 2026, Hans Huebner
   hans@huebner.org
   MIT license, see webserver.hpp for the full text.

   civetweb dedicates one worker thread to each live WebSocket and its writes
   block until the client's send buffer drains. A broadcaster that writes to
   every client under one lock therefore stalls on the slowest client - which
   freezes the producer and blocks the close handler that would free the
   thread, so vanished clients pile up until the 16-thread pool is exhausted
   and the whole server stops answering.

   web_ws_try_send() breaks that chain: it skips a client whose buffer is full
   (it is behind or gone, and gets nothing this round rather than blocking
   everyone), and reports one whose write fails so the caller drops it. A
   client that a browser closed cleanly is reaped by civetweb; one that
   vanished without a close is reaped by the ping/pong timeout.
*/
#ifndef _WEBWS_HPP_
#define _WEBWS_HPP_

#include <cstddef>
#include "civetweb.h"

//   1 = sent, 0 = skipped (client behind), -1 = dead (caller should remove it)
static inline int web_ws_try_send(struct mg_connection *conn, int opcode,
		const char *data, size_t len) {
	int w = mg_connection_writable(conn);
	if (w < 0)
		return -1;
	if (w == 0)
		return 0;
	return (mg_websocket_write(conn, opcode, data, len) > 0) ? 1 : -1;
}

#endif // _WEBWS_HPP_
