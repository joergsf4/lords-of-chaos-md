#!/usr/bin/env python3
# /// script
# requires-python = ">=3.11"
# dependencies = []
# ///
"""
Help, tutorial, lexicon and spell texts for the Mega Drive frontend (md/).

    uv run tools/md_help.py

Sources: md/data/help_md.txt, md/data/tutorial_md.txt (written for the pad, 26 columns), and the Agon
texts data/help/lexicon_de.txt and spells_de.txt (38 columns: re-wrapped paragraph by paragraph).
Text format: "= Titel" starts a page, # lines are comments, a blank line separates paragraphs.

Output md/res/<name>.bin (BIN resources): u16 big-endian page count, then per page
  u8 title_len, title, u8 line_count, per line u8 len + bytes   (ASCII only: umlauts become ae/oe/ue/ss)
"""
from __future__ import annotations

import struct
import sys
import textwrap
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
RES = ROOT / "md" / "res"
COLS = 26
MAX_LINES = 22
TR = str.maketrans({"ä": "ae", "ö": "oe", "ü": "ue", "ß": "ss", "Ä": "Ae", "Ö": "Oe", "Ü": "Ue", "–": "-", "—": "-",
                    "„": '"', "“": '"', "’": "'"})


def parse(path: Path):
    pages, cur = [], None
    for raw in path.read_text(encoding="utf-8").splitlines():
        if raw.startswith("#"):
            continue
        if raw.startswith("= "):
            cur = [raw[2:].strip().translate(TR), []]
            pages.append(cur)
        elif cur is not None:
            cur[1].append(raw.translate(TR).rstrip())
    for p in pages:
        while p[1] and not p[1][-1]:
            p[1].pop()
    return pages


def rewrap(lines):
    """Join the lines of each paragraph and wrap again to COLS."""
    out, para = [], []

    def flush():
        if para:
            out.extend(textwrap.wrap(" ".join(para), COLS, break_long_words=True))
            para.clear()

    for ln in lines:
        if ln.strip():
            para.append(ln.strip())
        else:
            flush()
            out.append("")
    flush()
    return out


def write(name, pages, wrap):
    blob = bytearray(struct.pack(">H", len(pages)))
    for title, lines in pages:
        # pages with aligned columns (two blanks inside a line) are kept as written
        if wrap and not any("  " in ln.strip() for ln in lines):
            lines = rewrap(lines)
        assert len(title) <= 26, title
        assert len(lines) <= MAX_LINES, f"{name}: page {title!r} has {len(lines)} lines"
        for ln in lines:
            assert len(ln) <= COLS, f"{name}: {ln!r} is {len(ln)} columns"
            assert all(32 <= ord(c) < 127 for c in ln), f"{name}: non-ASCII in {ln!r}"
        blob += bytes([len(title)]) + title.encode("ascii") + bytes([len(lines)])
        for ln in lines:
            blob += bytes([len(ln)]) + ln.encode("ascii")
    (RES / f"{name}.bin").write_bytes(bytes(blob))
    print(f"[md_help] {name}: {len(pages)} pages, {len(blob)} bytes")


def main():
    RES.mkdir(parents=True, exist_ok=True)
    write("help_md", parse(ROOT / "md" / "data" / "help_md.txt"), True)
    write("tutorial_md", parse(ROOT / "md" / "data" / "tutorial_md.txt"), True)
    write("lexicon_md", parse(ROOT / "data" / "help" / "lexicon_de.txt"), True)
    write("spells_md", parse(ROOT / "data" / "help" / "spells_de.txt"), True)


if __name__ == "__main__":
    sys.exit(main())
