/* weblog.cpp: the web interface's log source

   Copyright (c) 2026, Hans Huebner
   hans@huebner.org
   MIT license, see webserver.hpp for the full text.
*/

#include "weblog.hpp"

logsource_c *weblog(void) {
	// Constructed on first use and never destroyed: endpoints log from
	// civetweb's threads, and a static destructor could unregister it from the
	// logger while one of them is still running.
	static logsource_c *instance = nullptr;
	if (instance == nullptr) {
		instance = new logsource_c();
		instance->log_label = "web";
	}
	return instance;
}
