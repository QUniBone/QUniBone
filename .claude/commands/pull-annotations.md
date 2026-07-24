---
description: Pull annotations from the merged reMarkable doc and apply them to sources
allowed-tools: Bash, Read, Edit, mcp__remarkable__*
---

Harvest my reMarkable annotations from the merged `<PROJECT>-docs` PDF and turn
them into edits to the source markdown. The docs live as one merged PDF (see
/push-docs), so annotations are on PDF pages that must be mapped back to sections
and source files.

First, keep the tablet awake for the whole batch. It uses kernel autosleep
(`/sys/power/autosleep`), so it deep-suspends whenever no wakelock is held and
SSH then times out — `systemd-inhibit` does NOT help. Hold a wakelock with a
watchdog timeout: `ssh remarkable 'echo "rm_sync 900000000000" >
/sys/power/wake_lock'`. If the first SSH times out the device was mid-suspend —
retry once. Release it at the very end (final step), even on failure.

1. Select what to pull:
   - `PROJECT=$(basename "$(git rev-parse --show-toplevel)")`; the document is
     `<PROJECT>-docs`.
   - Read the watermark `.claude/.rm-last-pull.json` (document name → last-pulled
     `modified` timestamp; missing = first pull) and the section map
     `.claude/.rm-docmap.json` (written by /push-docs: section title → source
     file → page range). If the docmap is missing, rebuild it with
     `python3 .claude/scripts/rm-merge-docs.py` (its manifest.json).
   - `remarkable_recent`; proceed only if `<PROJECT>-docs`'s `modified` is newer
     than the stored value (compare tablet timestamps, never wall-clock).
2. Locate the ink on the tablet (the MCP's rendering is unreliable for a large
   merged PDF, so map pages yourself first):
   - Get the document UUID from `remarkable_browse("/$PROJECT")`.
   - `ssh remarkable "ls $X/<uuid>/*.rm"` — each `.rm` file's basename is a
     page-id.
   - Fetch `$X/<uuid>.content`; its `cPages.pages[]` maps each page-id to a PDF
     page via `redir.value` (0-based → PDF page = value+1). A page-id with
     `redir=None` is a blank page you appended (no source — treat as a general
     note). Build the sorted list of annotated PDF pages.
   - Map each annotated PDF page to a section/source file using the docmap page
     ranges (`start_page`–`end_page`).
3. Read the ink. OCR (`remarkable_read` content_type="annotations") is garbage
   here — render images instead, and mind these MCP quirks:
   - Use **ink-only** `remarkable_image(document, page=k)` — do NOT set
     `render_merged` (it composites ink onto the WRONG page background).
   - The stroke renderer addresses ONLY the annotated pages, numbered 1..N in
     PDF-page order. So the k-th annotated page (sorted) is `page=k`, NOT its PDF
     page number.
   - Render **one page per call**. Concurrent `remarkable_image` calls crash the
     MCP server; if it disconnects, ask me to reconnect it (`/mcp`).
   - Small/near-empty `.rm` files may fail to render ("local stroke render
     unavailable" / SVG render_failed). Report which pages/sections could not be
     read and ask me what they were, rather than guessing.
   - For each rendered page, read the handwriting visually and note what text or
     bullet it points at (use the source file's content for context).
4. Classify each annotation: (a) direct edit instruction ("fix this",
   crossed-out text, rewording), (b) margin comment / answer to an open question,
   (c) highlight or mark with no text.
5. Present a summary table: annotation → section → source file → proposed action.
   WAIT for my approval before editing anything.
6. On approval, apply (a) as edits to the source markdown (match the repo's
   affirmative style — for answers to open questions, convert the question into a
   decision); collect (b) likewise or as review notes; leave (c) as context.
7. Update `.claude/.rm-last-pull.json`: set `<PROJECT>-docs` to its current
   `modified` timestamp. (Keep `.claude/.rm-*.json` gitignored — local state.)
8. Offer to run /push-docs so the tablet gets a clean, re-linked copy reflecting
   the edits. Note that the edits shift the merged PDF's pages, so the push
   replaces the annotated copy rather than updating it in place — which is fine,
   since the ink has just been harvested into the sources.
9. Release the wakelock:
   `ssh remarkable 'echo rm_sync > /sys/power/wake_unlock'`. Do this even if an
   earlier step failed.
