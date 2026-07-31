#!/usr/bin/env python3
"""Regenerate bom.csv from the PCB layout.

The PCB is the authoritative source for what gets built -- the schematic has
drifted from it in the past, and a hand-maintained BOM drifted further still.
Reading the board directly means the BOM cannot go stale.

Usage:  python3 tools/gen_bom.py
"""

import csv
import re
import sys
from collections import defaultdict
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
PCB = ROOT / "beogram-esp32.kicad_pcb"
BOM = ROOT / "bom.csv"

# Footprints that are board features, not purchasable parts.
NON_PARTS = ("TestPoint:",)


def tokenize(text):
    return re.findall(r'\(|\)|"(?:[^"\\]|\\.)*"|[^\s()]+', text)


def parse(tokens, i=0):
    out = []
    while i < len(tokens):
        t = tokens[i]
        if t == "(":
            sub, i = parse(tokens, i + 1)
            out.append(sub)
        elif t == ")":
            return out, i + 1
        else:
            out.append(t[1:-1] if t.startswith('"') else t)
            i += 1
    return out, i


def children(node, key):
    return [n for n in node if isinstance(n, list) and n and n[0] == key]


def main():
    if not PCB.exists():
        sys.exit(f"PCB not found: {PCB}")

    board = parse(tokenize(PCB.read_text()))[0][0]

    groups = defaultdict(list)
    for fp in children(board, "footprint"):
        lib = fp[1]
        ref = value = ""
        for prop in children(fp, "property"):
            if len(prop) > 2 and prop[1] == "Reference":
                ref = prop[2]
            elif len(prop) > 2 and prop[1] == "Value":
                value = prop[2]
        if lib.startswith(NON_PARTS):
            continue
        groups[(value, lib)].append(ref)

    rows = []
    for (value, footprint), refs in groups.items():
        rows.append(
            {
                "references": ",".join(sorted(refs)),
                "value": value,
                "footprint": footprint,
                "quantity": len(refs),
            }
        )
    rows.sort(key=lambda r: (r["footprint"], r["value"]))

    with BOM.open("w", newline="") as fh:
        writer = csv.DictWriter(
            fh, fieldnames=["references", "value", "footprint", "quantity"]
        )
        writer.writeheader()
        writer.writerows(rows)

    total = sum(r["quantity"] for r in rows)
    print(f"Wrote {BOM.relative_to(ROOT)}: {len(rows)} lines, {total} placements")


if __name__ == "__main__":
    main()
