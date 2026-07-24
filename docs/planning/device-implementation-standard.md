# Device implementation standard

**Status:** Standing — applies to every emulated device, new or reworked.

How emulated devices are implemented and validated. This is a process
requirement that sits above the individual device features
([serial-ports.md](serial-ports.md), [vcb01.md](vcb01.md), and any future
device).

## Current state

The DELQA work already follows this shape: `diagnostics/reference/` holds the
DELQA User's Guide OCR'd to text, its README names the sections that decide
emulation behaviour and points at the CZQNA/CZQNA diagnostic, and the emulation
was validated under XXDP. This standard makes that practice the rule for all
devices.

## Requirements

### Work from the original manual

- Each device is implemented against its **original DEC manual**, not secondary
  sources or inference.
- The manual is **kept in the repo** as reference material, so the implementation
  and its source sit together — as the **OCR'd PDF (the authority, with figures
  and tables) plus a `pdftotext` extract** for fast grep.
- If the manual PDF **has no text layer, one is added** as part of the work.
  Recipe (`diagnostics/reference/README.md`): `ocrmypdf --force-ocr`, then
  `pdftotext -layout` for the extract.
- Manuals live in a dedicated top-level **`manuals/`** directory. The existing
  DELQA reference under `diagnostics/reference/` migrates there.

### Validate with XXDP diagnostics

- Every device implementation is **validated by running its XXDP diagnostic**. The
  implementation is not done until the diagnostic passes clean.
- The device's reference notes name its diagnostic (e.g. CZQNA for the DELQA) and
  how to read a failure.
- Where a device has **no XXDP diagnostic**, any available **standalone/maintenance
  diagnostic** is used instead; a documented manual procedure is the fallback only
  when no diagnostic exists at all.

### Provide what the diagnostic needs

- Whatever the diagnostic **requires to run** — loopback plugs, connectors,
  cabling, an external fixture — is **available and used** during the XXDP run. A
  loopback-mode test is run with the loopback in place, not skipped.

### Applies to existing devices too

- The standard is **retroactive**: every existing device (DL11, KW11, RL02,
  `uda`, `delqa`, …) is brought up to it — manual in `manuals/`, diagnostic
  validation on record — as its own work item, not only new devices.

## Decisions

- **Keep OCR'd PDF + `pdftotext` extract** per manual.
- **Manuals in a top-level `manuals/`** directory; migrate the DELQA reference.
- **PDFs via Git LFS**, so clones stay lean; extracted text is committed normally.
- **No XXDP → any standalone diagnostic**, manual procedure only as last resort.
- **Backfill all existing devices** to the standard, each as its own work item.
- **Loopback for emulated lines** — implement the device's **internal loopback
  mode** in the emulation, and provide **external loopback by tying the TCP line's
  output back to its input** in software; no physical plug on a pure TCP line.

## Open questions

- Git LFS must work on both remotes — Forgejo (`code.netzhansa.com`) and the
  GitHub mirror. Confirm LFS is enabled on both and the mirror carries the objects.
- The backfill is a body of work — is it one tracked item per device, and what is
  the order (start with devices whose emulation is least manual-grounded)?
