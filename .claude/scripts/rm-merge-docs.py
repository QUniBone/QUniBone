#!/usr/bin/env python3
"""Merge project docs into one linked PDF for the reMarkable, plus a manifest.

reMarkable cannot navigate between separate documents, so cross-doc links only
work inside a single PDF. This merges README.md and every *.md under docs/ into
one document where:

- each source doc becomes an H1 section with a unique {#anchor};
- the doc's own headings are demoted one level so the injected H1 is the top;
- cross-file links (foo.md, resolved relative to the linking file) are rewritten
  to internal anchors (#foo). Out-of-set and external links are left untouched;
- pandoc + weasyprint render it with a table of contents and per-doc page breaks
  (xelatex is not required — this path uses HTML/CSS).

It also writes a manifest mapping each section to its source file and its page
range in the built PDF, so pull-annotations can map an annotated page back to
the right source file and heading.

Requires: pandoc, weasyprint, pdfinfo, pdftotext (poppler).

Usage:
  rm-merge-docs.py --root <repo> --title "<title>" --outdir <dir>
Outputs in <outdir>: merged.md, <title-slug>.pdf (also printed), manifest.json
"""
import argparse, json, os, re, subprocess, sys

FENCE = re.compile(r"^\s*(```|~~~)")
HEADING = re.compile(r"^(#{1,6})(\s+\S)")
LINK = re.compile(r"\]\(([^)]+)\)")

CSS = """
@page { margin: 2cm; }
body { font-size: 12pt; line-height: 1.45; font-family: sans-serif; }
h1,h2,h3,h4 { line-height: 1.2; }
h1 { page-break-before: always; border-bottom: 2px solid #000; padding-bottom: 3px; }
h1:first-of-type { page-break-before: avoid; }
nav#TOC { page-break-after: always; }
nav#TOC ul { list-style: none; padding-left: 1em; }
nav#TOC a { text-decoration: none; }
pre, code { font-family: monospace; font-size: 10pt; }
pre { white-space: pre-wrap; word-wrap: break-word; background: #f4f4f4; padding: 0.5em; border-radius: 3px; }
table { border-collapse: collapse; }
th, td { border: 1px solid #999; padding: 3px 6px; }
a { color: #000; }
img { max-width: 100%; }
"""


def discover(root):
    """README.md (if present) first, then every *.md under docs/, sorted."""
    rels = []
    if os.path.isfile(os.path.join(root, "README.md")):
        rels.append("README.md")
    docs = os.path.join(root, "docs")
    for dirpath, _dirs, files in os.walk(docs):
        for f in sorted(files):
            if f.endswith(".md"):
                rels.append(os.path.relpath(os.path.join(dirpath, f), root))
    # stable order: README first, then docs/ paths sorted
    head = [r for r in rels if r == "README.md"]
    tail = sorted(r for r in rels if r != "README.md")
    return head + tail


def anchors_for(rels):
    out, used = {}, set()
    for rel in rels:
        stem = os.path.splitext(os.path.basename(rel))[0]
        a = re.sub(r"[^A-Za-z0-9]+", "-", stem).strip("-").lower()
        if a in used:
            parent = os.path.basename(os.path.dirname(rel)) or "root"
            a = f"{parent}-{a}"
        used.add(a)
        out[rel] = a
    return out


def demote_and_split(text):
    """Pull a leading H1 as the section title; demote remaining headings by one."""
    title, in_fence, took, out = None, False, False, []
    for ln in text.splitlines():
        if FENCE.match(ln):
            in_fence = not in_fence
            out.append(ln); continue
        if not in_fence:
            m = HEADING.match(ln)
            if m:
                lvl = len(m.group(1))
                if not took and lvl == 1 and title is None:
                    title, took = ln[1:].strip(), True
                    continue
                out.append("#" * min(lvl + 1, 6) + ln[lvl:]); continue
        out.append(ln)
    return title, "\n".join(out)


def rewrite_links(text, srcdir, root, anchor_by_rel):
    def repl(m):
        target = m.group(1).strip().split()[0]
        base = target.split("#", 1)[0]
        if not base or base.startswith(("http://", "https://", "mailto:", "/", "#")):
            return m.group(0)
        rel = os.path.relpath(os.path.normpath(os.path.join(srcdir, base)), root)
        return f"](#{anchor_by_rel[rel]})" if rel in anchor_by_rel else m.group(0)
    return LINK.sub(repl, text)


def build_markdown(root, rels, anchor_by_rel):
    parts, sections = [], []
    for rel in rels:
        text = open(os.path.join(root, rel), encoding="utf-8").read()
        title, body = demote_and_split(text)
        title = title or os.path.splitext(os.path.basename(rel))[0]
        body = rewrite_links(body, os.path.dirname(os.path.join(root, rel)), root, anchor_by_rel)
        parts.append(f"# {title} {{#{anchor_by_rel[rel]}}}\n\n{body.strip()}\n")
        sections.append({"title": title, "anchor": anchor_by_rel[rel], "source": rel})
    return "\n\n".join(parts) + "\n", sections


def page_of_titles(pdf, titles):
    """Map each section title to the PDF page whose first line equals it."""
    npages = int(subprocess.run(["pdfinfo", pdf], capture_output=True, text=True)
                 .stdout.split("Pages:")[1].split()[0])
    starts = {}
    for p in range(1, npages + 1):
        txt = subprocess.run(["pdftotext", "-f", str(p), "-l", str(p), pdf, "-"],
                             capture_output=True, text=True).stdout
        lines = [l.strip() for l in txt.splitlines() if l.strip()]
        if lines and lines[0] in titles and lines[0] not in starts:
            starts[lines[0]] = p
    return npages, starts


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--root", required=True)
    ap.add_argument("--title", required=True)
    ap.add_argument("--outdir", required=True)
    args = ap.parse_args()

    for tool in ("pandoc", "weasyprint", "pdfinfo", "pdftotext"):
        if subprocess.run(["which", tool], capture_output=True).returncode != 0:
            sys.exit(f"missing required tool: {tool}")

    os.makedirs(args.outdir, exist_ok=True)
    rels = discover(args.root)
    if not rels:
        sys.exit("no docs found (README.md or docs/**/*.md)")
    anchor_by_rel = anchors_for(rels)
    md, sections = build_markdown(args.root, rels, anchor_by_rel)

    md_path = os.path.join(args.outdir, "merged.md")
    css_path = os.path.join(args.outdir, "merged.css")
    slug = re.sub(r"[^A-Za-z0-9]+", "-", args.title).strip("-")
    pdf_path = os.path.join(args.outdir, f"{slug}.pdf")
    open(md_path, "w", encoding="utf-8").write(md)
    open(css_path, "w").write(CSS)

    r = subprocess.run(
        ["pandoc", md_path, "-o", pdf_path, "--pdf-engine=weasyprint",
         "--css", css_path, "--toc", "--toc-depth=2",
         "--metadata", f"title={args.title}", "-s"],
        capture_output=True, text=True)
    if r.returncode != 0:
        sys.exit(f"pandoc failed:\n{r.stderr}")

    npages, starts = page_of_titles(pdf_path, {s["title"] for s in sections})
    ordered = [s for s in sections if s["title"] in starts]
    ordered.sort(key=lambda s: starts[s["title"]])
    for i, s in enumerate(ordered):
        s["start_page"] = starts[s["title"]]
        s["end_page"] = (starts[ordered[i + 1]["title"]] - 1) if i + 1 < len(ordered) else npages

    manifest = {"document": slug, "title": args.title, "pdf_pages": npages,
                "sections": ordered}
    open(os.path.join(args.outdir, "manifest.json"), "w").write(json.dumps(manifest, indent=2))

    print(pdf_path)
    print(f"{len(ordered)} sections, {npages} pages")


if __name__ == "__main__":
    main()
