#!/usr/bin/env python3
"""Apply the PCB-TODO fixes to beogram-esp32.kicad_pcb.

Run with KiCad's bundled Python, which is the only one carrying the pcbnew
module:

  /Applications/KiCad/KiCad.app/Contents/Frameworks/Python.framework/Versions/Current/bin/python3 tools/fix_pcb.py

Close the PCB editor first -- this rewrites the file, and an open editor will
overwrite it on save.

Uses KiCad's own board object model rather than touching the s-expression text,
so UUIDs, net codes, and cross-references stay intact.
"""

import sys
from pathlib import Path

import pcbnew

ROOT = Path(__file__).resolve().parent.parent
PCB = ROOT / "beogram-esp32.kicad_pcb"

MM = 1_000_000  # KiCad internal units are nanometres


def at(mm_x, mm_y):
    return (round(mm_x * MM), round(mm_y * MM))


# Footprints are matched by position: three of them currently share the
# reference "GND", so references cannot identify them.
RENAMES = {
    at(159.5, 113.75): "TP1",
    at(155.5, 67.5): "TP2",
    at(146.01, 67.5): "TP3",
    at(142.5, 113.75): "TP4",
    at(142.1, 99.55): "TP5",
    at(142.5, 105.181598): "TP6",
    at(159.5, 105.2): "TP7",
    at(159.75, 118.05): "J1",
    at(159.88, 72.6): "J2",
}

TOLERANCE = 2000  # nm; guards against float rounding in the stored positions


def find_by_position(board, target):
    tx, ty = target
    for fp in board.GetFootprints():
        p = fp.GetPosition()
        if abs(p.x - tx) <= TOLERANCE and abs(p.y - ty) <= TOLERANCE:
            return fp
    return None


def main():
    board = pcbnew.LoadBoard(str(PCB))
    changed = []

    # 1. Give every footprint a unique, valid reference designator.
    for target, new_ref in RENAMES.items():
        fp = find_by_position(board, target)
        if fp is None:
            sys.exit(f"No footprint at {target} -- aborting without saving")
        old = fp.GetReference()
        if old != new_ref:
            fp.SetReference(new_ref)
            changed.append(f"ref  {old!r} -> {new_ref}")

    # 2. Opto LED series resistors: 1k gave ~2.1mA, below the LTV-356T's 5mA
    #    CTR spec point. 470R gives ~4.5mA.
    for ref in ("R1", "R2"):
        fp = board.FindFootprintByReference(ref)
        if fp is None:
            sys.exit(f"{ref} not found -- aborting without saving")
        if fp.GetValue() != "470R":
            changed.append(f"val  {ref} {fp.GetValue()} -> 470R")
            fp.SetValue("470R")

    # 3. TP6's pad has no net, so the GND zone clears around it instead of
    #    connecting it. Tie it to GND like the other two ground pads.
    gnd = board.FindNet("GND")
    if gnd is None:
        sys.exit("No GND net on the board -- aborting without saving")
    tp6 = board.FindFootprintByReference("TP6")
    for pad in tp6.Pads():
        if pad.GetNetname() != "GND":
            changed.append(f"net  TP6.{pad.GetNumber()} {pad.GetNetname()!r} -> GND")
            pad.SetNet(gnd)

    board.BuildListOfNets()

    # 4. Refill the copper pours. The stored fill was computed while TP6 had no
    #    net, so the zone cleared around it rather than connecting it.
    pcbnew.ZONE_FILLER(board).Fill(board.Zones())
    changed.append("zone refill")

    pcbnew.SaveBoard(str(PCB), board)

    print(f"Applied {len(changed)} changes:")
    for line in changed:
        print("  " + line)


if __name__ == "__main__":
    main()
