---
description: Merge project docs into one linked PDF and push it to the reMarkable
allowed-tools: Bash, mcp__remarkable__*
---

Push project documentation to the reMarkable as a single linked PDF. reMarkable
cannot navigate between separate documents, so all docs are merged into one PDF
where cross-references become working internal links.

First, keep the tablet awake for the whole batch. It uses kernel autosleep
(`/sys/power/autosleep`), so it deep-suspends whenever no wakelock is held and
SSH then times out — `systemd-inhibit` does NOT help. Hold a wakelock with a
watchdog timeout (auto-releases if this run dies): `ssh remarkable 'echo
"rm_sync 900000000000" > /sys/power/wake_lock'`. If that first SSH times out the
device was mid-suspend — retry once, it wakes on the connection. Release the lock
at the very end (see final step), even on failure. This one lock also keeps the
MCP tools' own SSH calls reliable.

1. Determine the project name and tablet addressing:
   - `PROJECT=$(basename "$(git rev-parse --show-toplevel 2>/dev/null || pwd)")`
   - Read the configured root from `remarkable_status` (`root_path`, e.g.
     `/ProjectDocs`). Read tools are scoped to that root; write tools (`upload`,
     `mkdir`) and direct SSH are NOT — their paths are absolute. So writes use
     `<root_path>/$PROJECT`, scoped browsing uses `/$PROJECT`. The xochitl store
     is `/home/root/.local/share/remarkable/xochitl` (`$X`), via `ssh remarkable`.
   - The merged document is named `<PROJECT>-docs` (e.g. `QUniBone-docs`).
2. Ensure the project folder exists: `remarkable_browse("/")`, and if `$PROJECT`
   is missing, `remarkable_mkdir("$PROJECT", parent="<root_path>")` (`mkdir` does
   not create missing parents, so `<root_path>` must already exist).
3. Build the merged PDF and manifest:
   `python3 .claude/scripts/rm-merge-docs.py --root "$(git rev-parse --show-toplevel)"
    --title "<PROJECT> — Docs" --outdir /tmp/rm-push`
   The script merges README.md and every `docs/**/*.md` into one PDF (each doc an
   H1 section with an anchor, cross-file `.md` links rewritten to internal jumps,
   a table of contents, per-doc page breaks) using pandoc + weasyprint — xelatex
   is not needed. It writes `/tmp/rm-push/manifest.json` mapping each section to
   its source file and page range. If pandoc, weasyprint, pdfinfo, or pdftotext
   is missing the script says so — stop and tell me. If `$ARGUMENTS` names files,
   mention that this workflow always merges the full doc set (the merged PDF is
   whole-set by design); confirm with me before narrowing.
4. Decide **replace** vs **create** by looking for `<PROJECT>-docs` in the
   project folder (`remarkable_browse("/$PROJECT")`).
   - **Not present — create:** `remarkable_upload(<pdf>,
     parent_folder="<root_path>/$PROJECT", document_name="<PROJECT>-docs")`.
   - **Present — replace.** A content change shifts the merged PDF's page layout,
     so existing annotations cannot be re-anchored. FIRST check whether the
     tablet copy has un-pulled annotations: get its UUID from the browse, and
     `ssh remarkable "ls $X/<uuid>/*.rm"`. If any `.rm` exist, STOP and tell me to
     run /pull-annotations first — pushing would strand that ink. If there are
     none (or I confirm the annotations are already harvested), replace: trash the
     old document (set `parent:"trash"`, `deleted:true` in its metadata, keep a
     trailing newline, restart xochitl) and upload the new PDF.
5. Copy the manifest to `.claude/.rm-docmap.json` so /pull-annotations can map
   annotated pages back to source files:
   `cp /tmp/rm-push/manifest.json .claude/.rm-docmap.json` (and add the manifest
   UUID/name context if the document was newly created). Ensure `.claude/.rm-*.json`
   is gitignored — it is local per-checkout state.
6. Report: created or replaced, the page count, the section→page map from the
   manifest, and anything that failed.
7. Release the wakelock:
   `ssh remarkable 'echo rm_sync > /sys/power/wake_unlock'`. Do this even if an
   earlier step failed; the watchdog timeout is only a fallback for a crashed run.

Note: tap-test an internal link on the tablet after a push — current firmware
follows PDF named-destination links, but only you can confirm on the device.
