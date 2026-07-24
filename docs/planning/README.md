# QBone — next development round

Requirements for the next round of QBone work, refined here before any code is
written. Each area of work has its own document. Every document captures what
the feature must do, where the code stands today, and an **Open questions**
section that drives the next round of refinement.

These are requirements, not designs. They say what the software must do and
why. Implementation plans (the `*-plan.md` files at the repo root, e.g.
[`vcb01-plan.md`](../../vcb01-plan.md)) come after an area's requirements
settle. The shipped web design lives in [`10.05_web/docs/plan.md`](../../10.05_web/docs/plan.md).

## How we work here

1. Ideas land in the relevant area document as draft requirements.
2. Open questions collect what still needs a decision.
3. Once an area's questions are answered, it is marked ready and an
   implementation plan follows.

Status legend, shown at the top of each document:

- **Gathering** — requirements still being collected; open questions unresolved.
- **Ready** — requirements settled; ready for an implementation plan.
- **In progress** — implementation underway.

## Areas of work

| Area | Document | Status |
|---|---|---|
| Configuration model | [configuration-model.md](configuration-model.md) | Ready — [plan](../../configuration-model-plan.md) |
| Web: configuration management | [web-config-management.md](web-config-management.md) | Gathering |
| Web: dashboard | [web-dashboard.md](web-dashboard.md) | Gathering |
| Web: navigation and URL state | [web-navigation.md](web-navigation.md) | Gathering |
| Console | [console.md](console.md) | Gathering |
| Device metadata (friendly names) | [device-metadata.md](device-metadata.md) | Gathering |
| Serial ports over TCP | [serial-ports.md](serial-ports.md) | Gathering |
| VCB01 support | [vcb01.md](vcb01.md) | Gathering |
| TMSCP tape controller | [tmscp-tape.md](tmscp-tape.md) | Gathering |
| Logging control | [logging.md](logging.md) | Gathering |
| RSX-11M+ over DELQA | [rsx-delqa.md](rsx-delqa.md) | Gathering |
| MCP server | [mcp-server.md](mcp-server.md) | Gathering |
| Device implementation standard | [device-implementation-standard.md](device-implementation-standard.md) | Standing |

## Cross-cutting decisions

These apply across the areas above and are fixed for this round:

- **Frontend gains a build step.** The web UI moves to **Vite + Preact +
  TypeScript**, bundled on the dev machine; the static output is still served
  as-is by civetweb, so nothing new runs on the board. All the web areas
  (dashboard, config management, navigation, console) build on this.
- **The REST API may change shape.** The web UI, MCP server, and board version
  together — one deployment, no external clients — so endpoints are reshaped
  freely as the configuration model needs, and `api.md` is updated to match.
- **New API work carries automated tests.** New endpoints, the configuration
  model above all, ship with integration tests runnable in CI (Forgejo Actions).
  Device emulation is validated by XXDP per the
  [device implementation standard](device-implementation-standard.md).

## Cross-cutting dependencies

The configuration model underpins the configuration-management UI and the
default-configuration and save/rename/delete behaviour. Friendly device names
surface in both the dashboard and the configuration-management screens. The
dashboard disk widgets depend on the same image list the configuration screens
manage.

**Implementation order:** the **configuration model is planned and built first**,
as the keystone the config UI, dashboard drive-swap, and MCP config tools sit on.
