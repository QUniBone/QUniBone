/* webapi.cpp: JSON API of the web interface

   Copyright (c) 2026, Hans Huebner
   hans@huebner.org
   MIT license, see webserver.hpp for the full text.

   Reads are snapshots of the device/parameter registry taken under
   device_c::mydevices_mutex. Writes go directly through the parameter
   system — the same calls the devices menu makes — serialized against the
   menu thread by device_configuration_c::operations_mutex:

     PUT  /api/devices/<device>/params/<param>   {"value": "..."}
     POST /api/control                           {"action": "init" | ...}

   Enable/disable is the "enabled" parameter, so the params endpoint covers
   it too.
*/

#include <string.h>

#include "civetweb.h"
#include "picojson.h"

#include "logger.hpp"
#include "device.hpp"
#include "storagedrive.hpp"
#include "storagecontroller.hpp"
#include "parameter.hpp"
#include "qunibus.h"
#include "qunibusadapter.hpp"
#include "panel.hpp"
#include "mscp_server.hpp"
#include "device_configuration.hpp"

#include "webevents.hpp"
#include "webconsole.hpp"
#include "webconsole_ext.hpp"
#include "webstorage.hpp"
#include "webconfigs.hpp"
#include "websettings.hpp"

static void send_json(struct mg_connection *conn, int status, const picojson::value &val) {
	std::string body = val.serialize();
	mg_printf(conn,
			"HTTP/1.1 %d %s\r\n"
			"Content-Type: application/json\r\n"
			"Cache-Control: no-store\r\n"
			"Content-Length: %u\r\n\r\n",
			status, status == 200 ? "OK" : "Error", (unsigned) body.size());
	mg_write(conn, body.c_str(), body.size());
}

static void send_error(struct mg_connection *conn, int status, const std::string &message) {
	picojson::object err;
	err["error"] = picojson::value(message);
	send_json(conn, status, picojson::value(err));
}

// read and parse a JSON object request body
static bool read_json_body(struct mg_connection *conn, picojson::value *out) {
	char body[4096];
	int body_len = mg_read(conn, body, sizeof(body) - 1);
	if (body_len <= 0)
		return false;
	body[body_len] = 0;
	std::string parse_err = picojson::parse(*out, body);
	return parse_err.empty() && out->is<picojson::object>();
}

// one parameter with metadata, typed values serialized from the value
// members (render() mutates shared state and is left to the menu thread)
static picojson::value param_to_json(parameter_c *p) {
	picojson::object o;
	o["name"] = picojson::value(p->name);
	o["shortname"] = picojson::value(p->shortname);
	o["readonly"] = picojson::value(p->readonly);
	if (!p->info.empty())
		o["info"] = picojson::value(p->info);
	if (!p->unit.empty())
		o["unit"] = picojson::value(p->unit);

	if (parameter_string_c *ps = dynamic_cast<parameter_string_c *>(p)) {
		o["type"] = picojson::value("string");
		o["value"] = picojson::value(ps->value);
	} else if (parameter_bool_c *pb = dynamic_cast<parameter_bool_c *>(p)) {
		o["type"] = picojson::value("bool");
		o["value"] = picojson::value(pb->value);
	} else if (parameter_unsigned_c *pu = dynamic_cast<parameter_unsigned_c *>(p)) {
		o["type"] = picojson::value("unsigned");
		o["value"] = picojson::value((double) pu->value);
		o["base"] = picojson::value((double) pu->base);
		o["bitwidth"] = picojson::value((double) pu->bitwidth);
	} else if (parameter_unsigned64_c *pu64 = dynamic_cast<parameter_unsigned64_c *>(p)) {
		o["type"] = picojson::value("unsigned64");
		o["value"] = picojson::value((double) pu64->value);
		o["base"] = picojson::value((double) pu64->base);
		o["bitwidth"] = picojson::value((double) pu64->bitwidth);
	} else if (parameter_double_c *pd = dynamic_cast<parameter_double_c *>(p)) {
		o["type"] = picojson::value("double");
		o["value"] = picojson::value(pd->value);
	} else {
		o["type"] = picojson::value("unknown");
	}
	return picojson::value(o);
}

// infrastructure: part of the bridge or of a controller's implementation,
// not of the emulated configuration — not exposed to the web interface.
// mscp_server is the UDA50's protocol engine, a device_c only so the
// logging macros work.
static bool device_is_infrastructure(device_c *d) {
	return dynamic_cast<qunibusadapter_c *>(d) != nullptr
			|| dynamic_cast<paneldriver_c *>(d) != nullptr
			|| dynamic_cast<mscp_server *>(d) != nullptr;
}

// caller holds device_c::mydevices_mutex
static device_c *find_device(const std::string &name) {
	for (device_c *d : device_c::mydevices) {
		if (device_is_infrastructure(d))
			continue;
		if (strcasecmp(d->name.value.c_str(), name.c_str()) == 0)
			return d;
	}
	return nullptr;
}

// GET /api/devices — snapshot of the device registry
static void devices_list(struct mg_connection *conn) {
	picojson::array devices;
	{
		std::lock_guard<std::mutex> lock(device_c::mydevices_mutex);
		for (device_c *d : device_c::mydevices) {
			if (device_is_infrastructure(d))
				continue;
			picojson::object o;
			o["name"] = picojson::value(d->name.value);
			o["type"] = picojson::value(d->type_name.value);
			o["category"] = picojson::value(std::string(d->category()));
			storagedrive_c *drv = dynamic_cast<storagedrive_c *>(d);
			if (drv != nullptr) {
				o["removable"] = picojson::value(drv->removable());
				o["locked"] = picojson::value(drv->locked());
			}
			o["enabled"] = picojson::value(d->enabled.value);
			o["parent"] = d->parent ?
					picojson::value(d->parent->name.value) : picojson::value();
			picojson::array params;
			for (parameter_c *p : d->parameter)
				params.push_back(param_to_json(p));
			o["params"] = picojson::value(params);
			devices.push_back(picojson::value(o));
		}
	}
	send_json(conn, 200, picojson::value(devices));
}

// PUT /api/devices/<device>/params/<param> {"value": ...} — set a parameter
static void device_param_set(struct mg_connection *conn, const std::string &devname,
		const std::string &paramname) {
	picojson::value req;
	if (!read_json_body(conn, &req) || req.get("value").is<picojson::null>()) {
		send_error(conn, 400, "body must be a JSON object with a \"value\" member");
		return;
	}
	// accept string, number and bool values; the parameter parses text
	std::string value;
	picojson::value v = req.get("value");
	if (v.is<std::string>())
		value = v.get<std::string>();
	else if (v.is<bool>())
		value = v.get<bool>() ? "1" : "0";
	else if (v.is<double>()) {
		char buff[40];
		snprintf(buff, sizeof(buff), "%g", v.get<double>());
		value = buff;
	} else {
		send_error(conn, 400, "\"value\" must be a string, number or bool");
		return;
	}

	device_c *dev;
	parameter_c *param;
	{
		std::lock_guard<std::mutex> lock(device_c::mydevices_mutex);
		dev = find_device(devname);
		param = dev ? dev->param_by_name(paramname) : nullptr;
	}
	if (dev == nullptr) {
		send_error(conn, 404, "unknown device \"" + devname + "\"");
		return;
	}
	if (param == nullptr) {
		send_error(conn, 404, "unknown parameter \"" + paramname + "\"");
		return;
	}

	{
		// same serialization as one command in the devices menu
		std::lock_guard<std::mutex> ops_lock(device_configuration_c::operations_mutex);
		try {
			if (param == &dev->enabled) {
				// "enabled" is a readonly parameter; the menu switches it
				// with the en/dis commands via set(), and so does the web
				bool on;
				if (value == "1" || !strcasecmp(value.c_str(), "true"))
					on = true;
				else if (value == "0" || !strcasecmp(value.c_str(), "false"))
					on = false;
				else {
					send_error(conn, 400, "\"enabled\" must be 1/0 or true/false");
					return;
				}
				dev->enabled.set(on);
				// disabling a controller disables the drives it contains:
				// a drive on a removed controller cannot function
				storagecontroller_c *ctrl = dynamic_cast<storagecontroller_c *>(dev);
				if (!on && ctrl != nullptr)
					for (storagedrive_c *drv : ctrl->storagedrives)
						if (drv != nullptr && drv->enabled.value) {
							drv->enabled.set(false);
							printf("\nweb: %s disabled with controller %s\n",
									drv->name.value.c_str(), dev->name.value.c_str());
						}
			} else
				param->parse(value);
			// keep the terminal user informed, like an echoed command
			printf("\nweb: %s.%s = %s\n", dev->name.value.c_str(),
					param->name.c_str(), value.c_str());
		} catch (bad_parameter &e) {
			printf("\nweb: %s.%s = %s rejected: %s\n", dev->name.value.c_str(),
					param->name.c_str(), value.c_str(), e.what());
			send_error(conn, 422, e.what());
			return;
		}
	}
	send_json(conn, 200, param_to_json(param));
}

// /api/devices and /api/devices/<device>/params/<param>
static int api_devices_handler(struct mg_connection *conn, void * /*cbdata*/) {
	const struct mg_request_info *ri = mg_get_request_info(conn);
	std::string uri = ri->local_uri ? ri->local_uri : "";
	std::string rest = uri.substr(strlen("/api/devices"));

	if (rest.empty() || rest == "/") {
		if (strcmp(ri->request_method, "GET") != 0) {
			send_error(conn, 405, "GET required");
			return 405;
		}
		devices_list(conn);
		return 200;
	}

	// expect /<device>/params/<param>
	std::vector<std::string> seg;
	size_t pos = 1;
	while (pos <= rest.size()) {
		size_t next = rest.find('/', pos);
		if (next == std::string::npos)
			next = rest.size();
		if (next > pos)
			seg.push_back(rest.substr(pos, next - pos));
		pos = next + 1;
	}
	if (seg.size() != 3 || seg[1] != "params") {
		send_error(conn, 404, "unknown path");
		return 404;
	}
	if (strcmp(ri->request_method, "PUT") != 0 && strcmp(ri->request_method, "POST") != 0) {
		send_error(conn, 405, "PUT required");
		return 405;
	}
	device_param_set(conn, seg[0], seg[2]);
	return 200;
}

// POST /api/control {"action": "init"|"powercycle"|"halt"|"continue"}
static int api_control_handler(struct mg_connection *conn, void * /*cbdata*/) {
	const struct mg_request_info *ri = mg_get_request_info(conn);
	if (strcmp(ri->request_method, "POST") != 0) {
		send_error(conn, 405, "POST required");
		return 405;
	}
	picojson::value req;
	if (!read_json_body(conn, &req) || !req.get("action").is<std::string>()) {
		send_error(conn, 400, "body must be a JSON object with an \"action\" string");
		return 400;
	}
	std::string action = req.get("action").get<std::string>();

	{
		std::lock_guard<std::mutex> ops_lock(device_configuration_c::operations_mutex);
		if (action == "init")
			qunibus->init();
		else if (action == "powercycle")
			qunibus->powercycle();
#if defined(QBUS)
		else if (action == "halt") {
			qunibus->set_halt(1);
			webevents_note_halt(true);
		} else if (action == "continue") {
			qunibus->set_halt(0);
			webevents_note_halt(false);
		}
#endif
		else {
			send_error(conn, 400, "unknown action \"" + action + "\"");
			return 400;
		}
		printf("\nweb: control %s\n", action.c_str());
	}
	picojson::object res;
	res["ok"] = picojson::value(true);
	send_json(conn, 200, picojson::value(res));
	return 200;
}

// called by webserver_c::start(); the host test build registers fixtures instead
void webapi_register(struct mg_context *ctx) {
	mg_set_request_handler(ctx, "/api/devices", api_devices_handler, nullptr);
	mg_set_request_handler(ctx, "/api/control", api_control_handler, nullptr);
	webstorage_register(ctx);
	webconfigs_register(ctx);
	websettings_register(ctx);
	webevents_register(ctx);
	webconsole_register(ctx);
	webconsole_ext_register(ctx);
	// apply the persisted external-console setting (loaded by
	// websettings_register) now that the bridge is up
	external_console_c ec = websettings_external_console();
	webconsole_ext_configure(ec.source, ec.port, ec.baud);
}

// called by webserver_c::stop() before the connections close
void webapi_shutdown(void) {
	webconsole_ext_shutdown();
	webconsole_shutdown();
	webevents_shutdown();
}
