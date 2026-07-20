/* webconfigs.hpp: /api/configs — named device-setup snapshots

   Copyright (c) 2026, Hans Huebner
   hans@huebner.org
   MIT license, see webserver.hpp for the full text.
*/
#ifndef _WEBCONFIGS_HPP_
#define _WEBCONFIGS_HPP_

#include <string>
#include <vector>

struct mg_context;

// register /api/configs; configurations live in $QUNIBONE_DIR/configs
void webconfigs_register(struct mg_context *ctx);

// name of a saved configuration whose image parameters reference the
// image file name; empty if none
std::string webconfigs_image_referenced(const std::string &image_name);

// one place a saved configuration puts an image: the drive it names it for
struct config_image_use_t {
	std::string config;
	std::string device;
};

// Apply a saved configuration to the device set, as the apply endpoint does.
// False when the configuration cannot be read, with the reason in "error";
// parameters the devices reject are collected in "rejections" and do not fail
// the call. The web server must be started first: registering it is what finds
// the configuration directory and captures the parameter defaults.
bool webconfigs_apply(const std::string &name, std::vector<std::string> *rejections,
		std::string *error);

// every configuration/drive pair that names this image file
std::vector<config_image_use_t> webconfigs_image_uses(const std::string &image_name);

#endif // _WEBCONFIGS_HPP_
