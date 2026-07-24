# Configuration model

**Status:** Gathering

The concept of a *current configuration* as a first-class object, so the running
machine's setup can be edited, saved back, named, and designated as the default.

## Current state

Configurations are saved device snapshots under `/var/lib/bone/configs`, applied
with `POST /api/configs/<name>/apply`. The live machine is a set of devices with
parameters; there is no notion of *which* configuration it currently represents.
"Current configuration" is inferred by comparing live devices against saved
snapshots, which cannot express an edited-but-unsaved state.

## Requirements

- There is always a **current configuration** — a named object the running
  machine represents, not something derived by comparison.
- The current configuration can be edited (devices and parameters change) and
  **saved back** under its own name.
- A configuration can be **renamed**.
- Any configuration other than the current one can be **deleted**.
- One configuration is the **designated default**, applied on startup.
- The default is **protected from deletion** — the user must designate another
  default before deleting the current one, so a valid default always exists.

## Decisions

- **Startup always applies the designated default configuration.** The current
  configuration is therefore always a named, saved config — there is no implicit
  unsaved "working" state. Edits are made against the current (named) config and
  saved back, or saved under a new name.
- **Edits are tracked as a dirty state.** When live devices diverge from the
  current config's last-saved form, the config is marked *modified*; the user
  clears it with **Save** (write back), **Save As** (new name), or **Revert**.
  Nothing auto-persists.

## Open questions

- There must always be a default. How is the initial default established on a
  board that has never had one set — a built-in fallback config, or first-run
  setup?
- The dirty/modified state must be exposed by the API for the UI to read. Is it
  computed by comparing live devices against the saved config, or tracked
  explicitly as edits happen?
- **Revert** restores the current config's saved form — does that re-apply it to
  the live machine (re-init devices), and what happens to a device that was added
  since the save?
- How does this interact with `settings.json`, which the service applies on
  startup today?
- Relationship to `211bsd.json` and the existing config files — migration, or do
  they become configurations in the new model as-is?
