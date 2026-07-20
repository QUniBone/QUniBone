/* webconfigs.cpp: /api/configs — named device-setup snapshots

   Copyright (c) 2026, Hans Huebner
   hans@huebner.org
   MIT license, see webserver.hpp for the full text.

   A configuration is a JSON snapshot of the emulated device setup, taken from
   and applied to the live parameter system — the same calls the devices menu
   and the REST parameter endpoint make.

   It describes the whole machine while naming as little as possible: the
   devices that are switched on, and of those only the parameters that differ
   from the values the devices were constructed with. Applying one therefore
   switches off every device it does not name and returns every parameter it
   does not name to that construction default.

     GET    /api/configs               list: name, mtime, enabled devices
     GET    /api/configs?current=1     the live setup in snapshot form, for
                                       comparison against the saved ones;
                                       503 while the machine is busy
     GET    /api/configs/<name>        full snapshot content
     PUT    /api/configs/<name>        save the current setup under <name>
     POST   /api/configs/<name>/apply  restore a snapshot (best effort,
                                       returns the rejections)
     DELETE /api/configs/<name>        remove a snapshot
     PUT    /api/configs/<name>/devices/<device>/image   {"value": "<image>"}
                                       the medium that drive starts with

   Files live in $QUNIBONE_DIR/configs/<name>.json:

     {"devices":[{"name":"RL11","enabled":true,
                  "params":{"address":"160010", ...}}, ...]}
*/

#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#include <fstream>
#include <list>
#include <map>
#include <mutex>
#include <set>
#include <sstream>
#include <string>

#include "civetweb.h"
#include "picojson.h"

#include "logger.hpp"
#include "device.hpp"
#include "parameter.hpp"
#include "qunibusadapter.hpp"
#include "panel.hpp"
#include "mscp_server.hpp"
#include "device_configuration.hpp"

#include "weblog.hpp"
#include "webconfigs.hpp"
#include "webstorage.hpp"

static std::string configs_dir;

static bool valid_config_name(const std::string &name) {
	if (name.empty() || name.size() > 64)
		return false;
	for (char c : name)
		if (!isalnum(c) && c != '-' && c != '_' && c != '.' && c != ' ')
			return false;
	return name[0] != '.';
}

static std::string config_path(const std::string &name) {
	return configs_dir + "/" + name + ".json";
}

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

static bool device_is_infrastructure(device_c *d) {
	return dynamic_cast<qunibusadapter_c *>(d) != nullptr
			|| dynamic_cast<paneldriver_c *>(d) != nullptr
			|| dynamic_cast<mscp_server *>(d) != nullptr;
}

// Parameter values as the devices were constructed. Captured once at
// registration, which application.cpp reaches directly after devices_startup()
// and before anything can have changed them, so this is what "default" means.
// A parameter absent from the map is always written.
//
// Writability is captured with it. A drive locks its image parameters while a
// pack spins, and that lock would otherwise drop the mounted image out of the
// configuration that needs it. What the device was built with says whether an
// operator may set it; what it reads now only says whether this moment suits.
struct param_default_t {
	std::string value;
	bool writable;
};
static std::map<parameter_c *, param_default_t> parameter_defaults;

static void capture_parameter_defaults(void) {
	std::lock_guard<std::mutex> lock(device_c::mydevices_mutex);
	for (device_c *dev : device_c::mydevices)
		for (parameter_c *p : dev->parameter) {
			param_default_t d;
			d.value = *p->render();
			d.writable = !p->readonly;
			parameter_defaults[p] = d;
		}
}

static bool is_default(parameter_c *p) {
	std::map<parameter_c *, param_default_t>::iterator it = parameter_defaults.find(p);
	return it != parameter_defaults.end() && it->second.value == *p->render();
}

// an operator may set this, whatever a transient lock says right now
static bool is_settable(parameter_c *p) {
	std::map<parameter_c *, param_default_t>::iterator it = parameter_defaults.find(p);
	return it == parameter_defaults.end() ? !p->readonly : it->second.writable;
}

// Put a device back the way it was constructed. Parameters named in "keep" are
// left alone, the caller being about to set them.
static void reset_to_defaults(device_c *dev, const std::set<std::string> *keep,
		picojson::array *errors) {
	for (parameter_c *p : dev->parameter) {
		if (p->readonly || is_default(p))
			continue;
		if (keep != nullptr && keep->count(p->name))
			continue;
		std::map<parameter_c *, param_default_t>::iterator it = parameter_defaults.find(p);
		if (it == parameter_defaults.end())
			continue;
		try {
			p->parse(it->second.value);
		} catch (bad_parameter &e) {
			if (errors != nullptr)
				errors->push_back(picojson::value(
						dev->name.value + "." + p->name + ": " + e.what()));
		}
	}
}

// A configuration describes the whole machine: it carries the devices that are
// switched on and, of those, only the parameters that differ from the
// construction defaults. Everything it does not mention is off and default.
//
// Caller holds operations_mutex and mydevices_mutex.
static picojson::value snapshot_devices_locked(void) {
	picojson::array devices;
	for (device_c *dev : device_c::mydevices) {
		if (device_is_infrastructure(dev) || !dev->enabled.value)
			continue;
		picojson::object o;
		o["name"] = picojson::value(dev->name.value);
		o["enabled"] = picojson::value(true);
		picojson::object params;
		for (parameter_c *p : dev->parameter) {
			if (!is_settable(p) || is_default(p))
				continue;
			params[p->name] = picojson::value(*p->render());
		}
		o["params"] = picojson::value(params);
		devices.push_back(picojson::value(o));
	}
	picojson::object root;
	root["devices"] = picojson::value(devices);
	return picojson::value(root);
}

// Saving is an explicit operator action and waits for the machine to be free.
static picojson::value snapshot_devices(void) {
	std::lock_guard<std::mutex> ops_lock(device_configuration_c::operations_mutex);
	std::lock_guard<std::mutex> lock(device_c::mydevices_mutex);
	return snapshot_devices_locked();
}

// The same snapshot for a status query, which must never block a worker thread
// waiting on the machine: both locks are polled to a deadline and released
// again if only one comes free, so a busy registry costs a 503 and not a wedged
// connection. Returns false if the machine stayed busy.
static bool snapshot_devices_now(picojson::value *out, unsigned timeout_ms) {
	std::unique_lock<std::mutex> ops_lock(device_configuration_c::operations_mutex,
			std::defer_lock);
	std::unique_lock<std::mutex> dev_lock(device_c::mydevices_mutex, std::defer_lock);
	for (unsigned waited = 0;; waited += 10) {
		if (ops_lock.try_lock()) {
			if (dev_lock.try_lock()) {
				*out = snapshot_devices_locked();
				return true;
			}
			ops_lock.unlock(); // never hold one while waiting for the other
		}
		if (waited >= timeout_ms)
			return false;
		usleep(10000);
	}
}

static bool read_config(const std::string &name, picojson::value *out,
		std::string *err) {
	std::ifstream in(config_path(name).c_str());
	if (!in.is_open()) {
		*err = "unknown configuration \"" + name + "\"";
		return false;
	}
	std::stringstream buffer;
	buffer << in.rdbuf();
	std::string parse_err = picojson::parse(*out, buffer.str());
	if (!parse_err.empty()) {
		*err = "unreadable configuration \"" + name + "\": " + parse_err;
		return false;
	}
	return true;
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

// the image a device entry names, empty when it names none
static std::string device_image(const picojson::value &d) {
	if (!d.get("params").is<picojson::object>())
		return "";
	const picojson::object &params = d.get("params").get<picojson::object>();
	picojson::object::const_iterator it = params.find("image");
	if (it == params.end() || !it->second.is<std::string>())
		return "";
	return it->second.get<std::string>();
}

static std::string basename_of(const std::string &path) {
	size_t base = path.rfind('/');
	return base == std::string::npos ? path : path.substr(base + 1);
}

// every drive, in every saved configuration, that names this image file
std::vector<config_image_use_t> webconfigs_image_uses(const std::string &image_name) {
	std::vector<config_image_use_t> uses;
	DIR *dir = opendir(configs_dir.c_str());
	if (dir == nullptr)
		return uses;
	struct dirent *entry;
	while ((entry = readdir(dir)) != nullptr) {
		std::string fname = entry->d_name;
		if (fname.size() < 6 || fname.compare(fname.size() - 5, 5, ".json") != 0)
			continue;
		std::string name = fname.substr(0, fname.size() - 5);
		picojson::value content;
		std::string err;
		if (!read_config(name, &content, &err)
				|| !content.get("devices").is<picojson::array>())
			continue;
		for (picojson::value &d : content.get("devices").get<picojson::array>()) {
			std::string path = device_image(d);
			if (path.empty() || basename_of(path) != image_name)
				continue;
			config_image_use_t use;
			use.config = name;
			use.device = d.get("name").is<std::string>()
					? d.get("name").get<std::string>() : "";
			uses.push_back(use);
		}
	}
	closedir(dir);
	return uses;
}

// deleting an image must not break a saved configuration
std::string webconfigs_image_referenced(const std::string &image_name) {
	std::vector<config_image_use_t> uses = webconfigs_image_uses(image_name);
	return uses.empty() ? "" : uses[0].config;
}

// GET /api/configs
static void configs_list(struct mg_connection *conn) {
	picojson::array configs;
	DIR *dir = opendir(configs_dir.c_str());
	if (dir != nullptr) {
		struct dirent *entry;
		while ((entry = readdir(dir)) != nullptr) {
			std::string fname = entry->d_name;
			if (fname.size() < 6 || fname.compare(fname.size() - 5, 5, ".json") != 0)
				continue;
			std::string name = fname.substr(0, fname.size() - 5);
			struct stat st;
			if (stat(config_path(name).c_str(), &st) != 0)
				continue;
			picojson::object o;
			o["name"] = picojson::value(name);
			char mtime[32];
			strftime(mtime, sizeof(mtime), "%Y-%m-%d %H:%M",
					localtime(&st.st_mtime));
			o["mtime"] = picojson::value(mtime);
			// the enabled devices, as the card summary
			picojson::value content;
			std::string err;
			picojson::array enabled;
			if (read_config(name, &content, &err) && content.get("devices").is<picojson::array>())
				for (picojson::value &d : content.get("devices").get<picojson::array>())
					if (d.get("enabled").is<bool>() && d.get("enabled").get<bool>())
						enabled.push_back(d.get("name"));
			o["enabled"] = picojson::value(enabled);
			configs.push_back(picojson::value(o));
		}
		closedir(dir);
	}
	send_json(conn, 200, picojson::value(configs));
}

// PUT /api/configs/<name> — save the current setup
static void config_save(struct mg_connection *conn, const std::string &name) {
	std::string body = snapshot_devices().serialize();
	std::ofstream out(config_path(name).c_str());
	if (!out.is_open()) {
		send_error(conn, 500, "cannot write configuration \"" + name + "\"");
		return;
	}
	out << body;
	out.close();
	WEB_INFO("configuration \"%s\" saved", name.c_str());
	picojson::object res;
	res["ok"] = picojson::value(true);
	send_json(conn, 200, picojson::value(res));
}

// PUT /api/configs/<name>/devices/<device>/image  {"value": "<image name>"}
//
// The medium a drive starts with belongs to the configuration, so it is
// editable there without disturbing the machine. An empty value leaves the
// drive with no image, which is the value it is constructed with, so the key
// is dropped rather than stored: a snapshot names only what differs.
//
// Two drives in one configuration must not name the same file — applying it
// would open the image twice, and both drives would write it.
static void config_set_image(struct mg_connection *conn, const std::string &name,
		const std::string &devname) {
	picojson::value req;
	if (!read_json_body(conn, &req) || !req.get("value").is<std::string>()) {
		send_error(conn, 400, "body must be a JSON object with a string \"value\"");
		return;
	}
	std::string value = req.get("value").get<std::string>();
	// a bare name is one of the images this interface manages
	std::string path = value.empty() ? "" : webstorage_image_path(value);

	picojson::value content;
	std::string err;
	if (!read_config(name, &content, &err)) {
		send_error(conn, 404, err);
		return;
	}
	if (!content.get("devices").is<picojson::array>()) {
		send_error(conn, 422, "configuration \"" + name + "\" names no devices");
		return;
	}
	picojson::array &devices = content.get<picojson::object>()["devices"]
			.get<picojson::array>();

	picojson::value *target = nullptr;
	for (picojson::value &d : devices) {
		if (!d.get("name").is<std::string>() || d.get("name").get<std::string>() != devname)
			continue;
		target = &d;
		break;
	}
	if (target == nullptr) {
		send_error(conn, 404, "configuration \"" + name + "\" does not name device \""
				+ devname + "\"");
		return;
	}
	if (!path.empty())
		for (picojson::value &d : devices) {
			if (&d == target)
				continue;
			if (device_image(d) != path)
				continue;
			std::string other = d.get("name").is<std::string>()
					? d.get("name").get<std::string>() : "another drive";
			send_error(conn, 409, "\"" + basename_of(path) + "\" is already the image of "
					+ other + " in this configuration");
			return;
		}

	if (!target->get("params").is<picojson::object>())
		target->get<picojson::object>()["params"] = picojson::value(picojson::object());
	picojson::object &params = target->get<picojson::object>()["params"]
			.get<picojson::object>();
	if (path.empty())
		params.erase("image");
	else
		params["image"] = picojson::value(path);

	std::ofstream out(config_path(name).c_str());
	if (!out.is_open()) {
		send_error(conn, 500, "cannot write configuration \"" + name + "\"");
		return;
	}
	out << content.serialize();
	out.close();
	WEB_INFO("configuration \"%s\": %s image = %s", name.c_str(),
			devname.c_str(), path.empty() ? "none" : path.c_str());
	picojson::object res;
	res["ok"] = picojson::value(true);
	res["image"] = picojson::value(path);
	send_json(conn, 200, picojson::value(res));
}

// POST /api/configs/<name>/apply — restore a snapshot. Devices are stored
// in registry order (controllers before their drives), so applying in
// order enables controllers first. Rejections are collected, not fatal.
// Apply a saved configuration to the device set: the work behind both
// POST /api/configs/<name>/apply and the --config option of the service.
// Returns false when the configuration cannot be read; parameters the devices
// reject are collected in "errors" and do not fail the call.
static bool apply_config(const std::string &name, picojson::array *errors,
		std::string *error) {
	picojson::value content;
	if (!read_config(name, &content, error))
		return false;
	if (!content.get("devices").is<picojson::array>()) {
		*error = "configuration \"" + name + "\" has no devices";
		return false;
	}
	{
		std::lock_guard<std::mutex> ops_lock(device_configuration_c::operations_mutex);

		// The configuration is the whole machine, so anything it leaves out is
		// switched off and back at its defaults. Work backwards through the
		// registry so drives go before the controllers they hang off.
		std::set<std::string> mentioned;
		for (picojson::value &d : content.get("devices").get<picojson::array>())
			if (d.get("name").is<std::string>())
				mentioned.insert(d.get("name").get<std::string>());
		{
			std::lock_guard<std::mutex> lock(device_c::mydevices_mutex);
			for (std::list<device_c *>::reverse_iterator it = device_c::mydevices.rbegin();
					it != device_c::mydevices.rend(); ++it) {
				device_c *dev = *it;
				if (device_is_infrastructure(dev))
					continue;
				bool named = false;
				for (const std::string &n : mentioned)
					if (strcasecmp(n.c_str(), dev->name.value.c_str()) == 0) {
						named = true;
						break;
					}
				if (named)
					continue;
				if (dev->enabled.value)
					dev->enabled.set(false);
				reset_to_defaults(dev, nullptr, errors);
			}
		}

		for (picojson::value &d : content.get("devices").get<picojson::array>()) {
			if (!d.get("name").is<std::string>())
				continue;
			std::string devname = d.get("name").get<std::string>();
			device_c *dev = nullptr;
			{
				std::lock_guard<std::mutex> lock(device_c::mydevices_mutex);
				for (device_c *cand : device_c::mydevices)
					if (!device_is_infrastructure(cand)
							&& strcasecmp(cand->name.value.c_str(), devname.c_str()) == 0) {
						dev = cand;
						break;
					}
			}
			if (dev == nullptr) {
				errors->push_back(picojson::value(devname + ": unknown device"));
				continue;
			}
			// parameters the file omits are at their defaults too
			std::set<std::string> listed;
			if (d.get("params").is<picojson::object>())
				for (const std::pair<const std::string, picojson::value> &kv :
						d.get("params").get<picojson::object>())
					listed.insert(kv.first);
			reset_to_defaults(dev, &listed, errors);

			if (d.get("params").is<picojson::object>())
				for (const std::pair<const std::string, picojson::value> &kv :
						d.get("params").get<picojson::object>()) {
					if (!kv.second.is<std::string>())
						continue;
					parameter_c *param = dev->param_by_name(kv.first);
					if (param == nullptr || param->readonly)
						continue;
					if (*param->render() == kv.second.get<std::string>())
						continue; // unchanged — don't disturb the device
					try {
						param->parse(kv.second.get<std::string>());
					} catch (bad_parameter &e) {
						errors->push_back(picojson::value(
								devname + "." + kv.first + ": " + e.what()));
					}
				}
			if (d.get("enabled").is<bool>())
				dev->enabled.set(d.get("enabled").get<bool>());
		}
	}
	WEB_INFO("configuration \"%s\" applied, %u rejections",
			name.c_str(), (unsigned) errors->size());
	return true;
}

// POST /api/configs/<name>/apply
static void config_apply(struct mg_connection *conn, const std::string &name) {
	picojson::array errors;
	std::string error;
	if (!apply_config(name, &errors, &error)) {
		send_error(conn, 404, error);
		return;
	}
	picojson::object res;
	res["ok"] = picojson::value(errors.empty());
	res["errors"] = picojson::value(errors);
	send_json(conn, 200, picojson::value(res));
}

bool webconfigs_apply(const std::string &name, std::vector<std::string> *rejections,
		std::string *error) {
	picojson::array errors;
	if (!apply_config(name, &errors, error))
		return false;
	if (rejections != nullptr)
		for (picojson::value &e : errors)
			rejections->push_back(e.is<std::string>() ? e.get<std::string>() : "?");
	return true;
}

// /api/configs, /api/configs/<name>, /api/configs/<name>/apply
static int api_configs_handler(struct mg_connection *conn, void * /*cbdata*/) {
	const struct mg_request_info *ri = mg_get_request_info(conn);
	std::string uri = ri->local_uri ? ri->local_uri : "";
	std::string rest = uri.substr(strlen("/api/configs"));
	std::string method = ri->request_method;

	if (rest.empty() || rest == "/") {
		if (method != "GET") {
			send_error(conn, 405, "GET required");
			return 405;
		}
		// ?current=1 renders the live setup in snapshot form, so a caller can
		// tell which saved configuration — if any — is the one loaded
		const char *query = ri->query_string;
		if (query != nullptr && strstr(query, "current") != nullptr) {
			picojson::value current;
			if (!snapshot_devices_now(&current, 500)) {
				send_error(conn, 503, "machine busy, current setup unavailable");
				return 503;
			}
			send_json(conn, 200, current);
		} else
			configs_list(conn);
		return 200;
	}

	std::string name = rest.substr(1);
	bool apply = false;
	// /<name>/devices/<device>/image
	std::string image_device;
	size_t devsep = name.find("/devices/");
	if (devsep != std::string::npos) {
		std::string tail = name.substr(devsep + strlen("/devices/"));
		name = name.substr(0, devsep);
		if (tail.size() < 7 || tail.compare(tail.size() - 6, 6, "/image") != 0) {
			send_error(conn, 404, "only the image of a device is editable");
			return 404;
		}
		image_device = tail.substr(0, tail.size() - 6);
		if (image_device.empty() || image_device.find('/') != std::string::npos) {
			send_error(conn, 404, "unknown device");
			return 404;
		}
	} else if (name.size() > 6 && name.compare(name.size() - 6, 6, "/apply") == 0) {
		name = name.substr(0, name.size() - 6);
		apply = true;
	}
	if (!valid_config_name(name)) {
		send_error(conn, 404, "unknown configuration");
		return 404;
	}

	if (!image_device.empty()) {
		if (method != "PUT") {
			send_error(conn, 405, "PUT required");
			return 405;
		}
		config_set_image(conn, name, image_device);
	} else if (apply && method == "POST")
		config_apply(conn, name);
	else if (!apply && method == "PUT")
		config_save(conn, name);
	else if (!apply && method == "GET") {
		picojson::value content;
		std::string err;
		if (!read_config(name, &content, &err))
			send_error(conn, 404, err);
		else
			send_json(conn, 200, content);
	} else if (!apply && method == "DELETE") {
		if (unlink(config_path(name).c_str()) != 0) {
			send_error(conn, 404, "unknown configuration \"" + name + "\"");
			return 404;
		}
		WEB_INFO("configuration \"%s\" deleted", name.c_str());
		picojson::object res;
		res["ok"] = picojson::value(true);
		send_json(conn, 200, picojson::value(res));
	} else {
		send_error(conn, 405, "unsupported method");
		return 405;
	}
	return 200;
}

void webconfigs_register(struct mg_context *ctx) {
	const char *base = getenv("QUNIBONE_DIR");
	if (base == nullptr)
		base = getenv("HOME");
	configs_dir = std::string(base ? base : ".") + "/configs";
	mkdir(configs_dir.c_str(), 0755); // may already exist
	capture_parameter_defaults();
	mg_set_request_handler(ctx, "/api/configs", api_configs_handler, nullptr);
}
