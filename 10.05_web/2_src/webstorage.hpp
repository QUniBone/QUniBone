/* webstorage.hpp: /api/images — disk image files of the web interface

   Copyright (c) 2026, Hans Huebner
   hans@huebner.org
   MIT license, see webserver.hpp for the full text.
*/
#ifndef _WEBSTORAGE_HPP_
#define _WEBSTORAGE_HPP_

struct mg_context;

// register /api/images; images live in $QUNIBONE_DIR/images
void webstorage_register(struct mg_context *ctx);

#endif // _WEBSTORAGE_HPP_
