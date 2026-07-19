# civetweb (vendored)

Embedded HTTP/WebSocket server for the QUniBone web interface (`10.05_web`).

- Source: https://github.com/civetweb/civetweb, release **v1.16** (MIT license,
  see `LICENSE.md`)
- Vendored files: `civetweb.c`, `civetweb.h` and the `.inl` fragments needed
  for a plain build; the Lua/Duktape/mbedTLS/wolfSSL/HTTP2/zlib modules are
  omitted.
- Build flags used by the demo makefiles: `-DNO_SSL -DUSE_WEBSOCKET -DNO_CGI`.
