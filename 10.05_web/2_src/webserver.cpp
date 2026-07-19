/* webserver.cpp: embedded HTTP/WebSocket server for the web interface

   Copyright (c) 2026, Hans Huebner
   hans@huebner.org

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
   THE AUTHOR BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
   IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
   CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
*/

#include <stdio.h>
#include <string.h>
#include <pthread.h>
#include <sched.h>

#include "civetweb.h"
#include "picojson.h"

#include "logger.hpp"
#include "webserver.hpp"

webserver_c *webserver = nullptr;

// force every civetweb thread to time-share scheduling. The device workers
// run SCHED_RR on this single-core machine; a web thread must never share
// that real-time band, or a dashboard request round-robins with the bus
// servicer and the DELQA reflection worker, blowing the firmware's ~33ms
// self-test poll window.
static void *webserver_init_thread(const struct mg_context *ctx, int thread_type) {
	(void) ctx;
	(void) thread_type;
	struct sched_param param;
	param.sched_priority = 0;
	pthread_setschedparam(pthread_self(), SCHED_OTHER, &param);
	return nullptr;
}

/*** HTTP basic auth, enabled by setting WEBUI_PASSWORD in the environment
     (export it from qunibone-platform.env). Any user name is accepted, the
     password must match. Unset = open, matching the bench-LAN trust model.
     Browsers replay the credentials on the WebSocket handshakes, so /ws/
     is covered as well. ***/

static std::string webui_password;

// decode base64 into out; result false on illegal input
static bool base64_decode(const char *in, std::string *out) {
	static const char *alphabet =
			"ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
	unsigned bits = 0, value = 0;
	out->clear();
	for (; *in && *in != '='; in++) {
		const char *pos = strchr(alphabet, *in);
		if (pos == nullptr)
			return false;
		value = (value << 6) | (pos - alphabet);
		bits += 6;
		if (bits >= 8) {
			bits -= 8;
			out->push_back((char) (value >> bits));
		}
	}
	return true;
}

// result 0: request continues (authorized or auth disabled), 1: rejected
static int begin_request_handler(struct mg_connection *conn) {
	if (webui_password.empty())
		return 0;
	const char *auth = mg_get_header(conn, "Authorization");
	if (auth != nullptr && strncmp(auth, "Basic ", 6) == 0) {
		std::string credentials; // "user:password", any user accepted
		if (base64_decode(auth + 6, &credentials)) {
			size_t colon = credentials.find(':');
			if (colon != std::string::npos
					&& credentials.compare(colon + 1, std::string::npos,
							webui_password) == 0)
				return 0;
		}
	}
	mg_printf(conn,
			"HTTP/1.1 401 Unauthorized\r\n"
			"WWW-Authenticate: Basic realm=\"QBone\"\r\n"
			"Content-Length: 0\r\n\r\n");
	return 1;
}

#if defined(QBUS)
static const char *platform_name = "QBUS";
#elif defined(UNIBUS)
static const char *platform_name = "UNIBUS";
#else
static const char *platform_name = "HOST"; // host-side test build
#endif

// GET /api/state — phase 0: identifies the platform and the API generation.
// Bus/device state fields are added with the corresponding phases.
static int api_state_handler(struct mg_connection *conn, void * /*cbdata*/) {
	picojson::object state;
	state["platform"] = picojson::value(platform_name);
	state["api_version"] = picojson::value((double)0);
	std::string body = picojson::value(state).serialize();
	mg_printf(conn,
			"HTTP/1.1 200 OK\r\n"
			"Content-Type: application/json\r\n"
			"Cache-Control: no-store\r\n"
			"Content-Length: %u\r\n\r\n", (unsigned) body.size());
	mg_write(conn, body.c_str(), body.size());
	return 200;
}

webserver_c::webserver_c(unsigned port, std::string docroot) :
		port(port), docroot(docroot) {
	log_label = "websrv";
}

webserver_c::~webserver_c() {
	stop();
}

bool webserver_c::start(void) {
	char portstr[16];
	snprintf(portstr, sizeof(portstr), "%u", port);
	// each connected WebSocket occupies one worker thread for its lifetime
	// (2 per open page: events + console), so the pool must cover several
	// browser sessions plus concurrent REST requests
	//
	// the frontend is a single file that changes with every deploy, so browsers
	// must revalidate it rather than serve an hour-old copy from cache; the
	// ETag makes that a 304 in the common case
	const char *options[] = { //
			"document_root", docroot.c_str(), //
			"listening_ports", portstr, //
			"num_threads", "16", //
			"enable_directory_listing", "no", //
			"static_file_max_age", "0", //
			nullptr };

	const char *password = getenv("WEBUI_PASSWORD");
	webui_password = password ? password : "";

	struct mg_callbacks callbacks;
	memset(&callbacks, 0, sizeof(callbacks));
	callbacks.begin_request = begin_request_handler;
	callbacks.init_thread = webserver_init_thread;

	mg_init_library(0);
	ctx = mg_start(&callbacks, nullptr, options);
	if (ctx == nullptr) {
		ERROR("web server failed to start on port %u, document root %s", port, docroot.c_str());
		mg_exit_library();
		return false;
	}
	mg_set_request_handler(ctx, "/api/state", api_state_handler, nullptr);
	webapi_register(ctx);
	INFO("web server listening on port %u, document root %s, %s", port, docroot.c_str(),
			webui_password.empty() ? "open access" : "basic auth enabled");
	return true;
}

void webserver_c::stop(void) {
	if (ctx == nullptr)
		return;
	webapi_shutdown();
	mg_stop(ctx);
	ctx = nullptr;
	mg_exit_library();
	INFO("web server stopped");
}
