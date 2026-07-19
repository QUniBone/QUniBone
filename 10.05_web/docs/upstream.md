# Upstream contribution plan

The `web-interface` branch of `hanshuebner/QUniBone` lands in
`j-hoppe/QUniBone` as three pull requests, each reviewable on its own.
The branch history mixes the concerns, so the PR branches are built by
applying path-scoped diffs of the final tree, in dependency order.

## PR 1 — core enablers

Small diffs to existing code. Each is inert without a consumer and
carries no behavior change for CLI-only use.

| Area | Change |
|---|---|
| `10.01_base/2_src/arm/device.*` | `mydevices_mutex` guards the device registry |
| `10.01_base/2_src/arm/parameter.*` | `parameter_c::change_hook`, fired after a committed value change |
| `90_common/src/logger.*` | `logger_c::message_sink`, receives each rendered message |
| `10.02_devices/2_src/rs232adapter.*` | `stream_xmt_tap`, a permanent second xmt sink |
| `10.02_devices/2_src/blinkenbone/blinkenbone_panel.cpp` | `disconnect()` tolerates a never-connected panel |
| `10.03_app_demo/2_src/device_configuration.*` (new) | device set extracted from the devices menu |
| `10.03_app_demo/2_src/menu_devices.cpp` | menu constructs/destroys the set through `device_configuration_c`, takes `operations_mutex` per command |
| `10.03_app_demo/2_src/makefile_q`, `makefile_u` | `device_configuration.o` |

In this PR the devices menu still owns the device set's lifetime; the
`operations_mutex` serializes commands against future concurrent callers.

## PR 2 — web interface

Everything new, depends on PR 1.

| Area | Change |
|---|---|
| `10.05_web/` | server sources, frontend, docs, host test |
| `91_3rd_party/civetweb/`, `91_3rd_party/picojson/` | vendored HTTP/WebSocket server and JSON library |
| `10.03_app_demo/2_src/application.*` | `--web [port]` option; `devices_startup()/devices_shutdown()` give the device set process lifetime in web mode |
| `10.03_app_demo/2_src/menus.cpp` | hardware test menus blocked while the web interface owns the devices |
| `10.03_app_demo/2_src/makefile_q`, `makefile_u` | `WEBUI` block (default on, `WEBUI=0` builds the unchanged demo) |

## PR 3 — cross-compilation support

Independent of the web interface.

| Area | Change |
|---|---|
| `crossbuild.sh` (new) | Docker-based cross build with fetch of PRU arrays/headers from the device, `-d` deploys |
| `10.03_app_demo/2_src/makefile_q`, `makefile_u` | `EXTRA_LIBS` hook (static libtirpc where glibc has no Sun RPC) |
| `10.01_base/2_src/arm/utils.hpp` | includes `<time.h>` for `struct tm` |
| `.gitignore` (new) | build output, fetched headers |

## Mechanics

At submission time, for each PR: branch from upstream `master`, apply the
path-scoped diff (`git diff master...web-interface -- <paths> | git
apply`), adjust the seams named above, build with `crossbuild.sh` and the
device's `compile.sh`, and verify `WEBUI=0` produces a behaviorally
unchanged demo.
