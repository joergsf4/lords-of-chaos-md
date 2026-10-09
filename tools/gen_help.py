#!/usr/bin/env python3
# /// script
# requires-python = ">=3.11"
# dependencies = []
# ///
"""
Compile data/help/<name>.txt into paged help files (M5, ADR 0011).

    uv run tools/gen_help.py

Outputs (generated, not committed):
  build/help/<name>.hlp   loaded from /loc/help on the SD card

Text format (UTF-8): "= Titel" starts a page, the following lines are its
body (max 26 lines, 38 columns). Umlauts are converted to the umfont glyph
codes (umfont.c): ae 0x84, oe 0x94, ue 0x81, ss 0xE1.

.hlp format (little endian, all text pre-encoded):
  "LOCH" | u8 version=1 | u16 page_count
  per page: u8 title_len | title | u8 line_count | per line: u8 len | bytes
"""

from __future__ import annotations

import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
SRC = ROOT / "data" / "help"
OUT_DIR = ROOT / "build" / "help"

MAX_COLS = 38   # 40 text columns, keep one clear at each edge
MAX_LINES = 26  # rows 2..27 between title and footer
UMLAUTS = {"ä": 0x84, "ö": 0x94, "ü": 0x81, "ß": 0xE1}


def encode_text(s: str) -> bytes:
    """UTF-8 line -> bytes with umfont glyph codes."""
    out = bytearray()
    for ch in s:
        if ch in UMLAUTS:
            out.append(UMLAUTS[ch])
        elif 32 <= ord(ch) < 127:
            out.append(ord(ch))
        else:
            raise SystemExit(f"unencodable character {ch!r} (use ae/oe/ue/ss)")
    return bytes(out)


def parse(path: Path) -> list[tuple[bytes, list[bytes]]]:
    pages: list[tuple[bytes, list[bytes]]] = []
    title: bytes | None = None
    body: list[bytes] = []

    def flush() -> None:
        nonlocal title, body
        if title is not None:
            pages.append((title, body))
        title, body = None, []

    for raw in path.read_text(encoding="utf-8").splitlines():
        line = raw.rstrip()
        if line.startswith("#"):
            continue
        if line.startswith("= "):
            flush()
            title = encode_text(line[2:39].strip())
            continue
        if not line.strip():
            continue
        if title is None:
            raise SystemExit(f"{path.name}: body line before any '= title'")
        if len(line) > MAX_COLS:
            raise SystemExit(f"{path.name}: line longer than {MAX_COLS} columns: {line!r}")
        body.append(encode_text(line.rstrip()))
        if len(body) > MAX_LINES:
            raise SystemExit(f"{path.name}: more than {MAX_LINES} body lines on one page")
    flush()
    if not pages:
        raise SystemExit(f"{path.name}: no pages")
    return pages


def encode(pages: list[tuple[bytes, list[bytes]]]) -> bytes:
    out = bytearray(b"LOCH")
    out.append(1)
    out += len(pages).to_bytes(2, "little")
    for title, body in pages:
        out.append(len(title))
        out += title
        out.append(len(body))
        for text in body:
            out.append(len(text))
            out += text
    return bytes(out)


def csv_rows(path: Path) -> int:
    return sum(1 for line in path.read_text(encoding="utf-8").splitlines()
               if line and not line.startswith(("#", "id,")))


# Read buffers in src/agon/screens.c (HELP_MAX, LEXICON_MAX, SPELLS_MAX):
# a bigger file would be read cut off and rejected as invalid.
BUFFER_LIMIT = {"lexicon_de": 7168, "spells_de": 4600}
DEFAULT_LIMIT = 4096


def main() -> int:
    OUT_DIR.mkdir(parents=True, exist_ok=True)
    for path in sorted(SRC.glob("*.txt")):
        pages = parse(path)
        if path.stem == "lexicon_de":
            want = csv_rows(ROOT / "data" / "creatures.csv") + \
                csv_rows(ROOT / "data" / "objects.csv")
            if len(pages) != want:
                raise SystemExit(
                    f"{path.name}: {len(pages)} pages, expected {want} "
                    "(one per creature, then one per object, csv order)")
        data = encode(pages)
        limit = BUFFER_LIMIT.get(path.stem, DEFAULT_LIMIT)
        if len(data) > limit:
            raise SystemExit(f"{path.name}: {len(data)} bytes, the game buffer "
                             f"holds {limit} (screens.c)")
        (OUT_DIR / (path.stem + ".hlp")).write_bytes(data)
        print(f"[help] {path.name} -> build/help/{path.stem}.hlp "
              f"({len(pages)} pages, {len(data)} bytes)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
