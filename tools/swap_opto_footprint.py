#!/usr/bin/env python3
"""Move U2/U3 onto the correct LTV-356T land pattern.

The board used SO-4_4.4x4.3mm_P2.54mm, a Broadcom package with 0.8 x 1.2mm
pads. KiCad's own LTV-356T symbol specifies SO-4_4.4x3.6mm_P2.54mm, whose
2.0 x 0.64mm pads are the proper gull-wing lands -- long in X so they catch
both the heel and the toe of the lead and leave an inspectable fillet.

Pad numbering is identical between the two, so nets transfer one-for-one.
Pads sit 0.15mm further out in X, which the existing 0.5mm traces absorb.

Run with KiCad's bundled Python, with the PCB editor closed:

  /Applications/KiCad/KiCad.app/Contents/Frameworks/Python.framework/Versions/Current/bin/python3 tools/swap_opto_footprint.py
"""

import sys
from pathlib import Path

import pcbnew

ROOT = Path(__file__).resolve().parent.parent
PCB = ROOT / "beogram-esp32.kicad_pcb"

LIB = "/Applications/KiCad/KiCad.app/Contents/SharedSupport/footprints/Package_SO.pretty"
NEW_FP = "SO-4_4.4x3.6mm_P2.54mm"
TARGETS = ("U2", "U3")


def main():
    board = pcbnew.LoadBoard(str(PCB))

    for ref in TARGETS:
        old = board.FindFootprintByReference(ref)
        if old is None:
            sys.exit(f"{ref} not found -- aborting without saving")

        if str(old.GetFPID().GetLibItemName()) == NEW_FP:
            print(f"{ref} already on {NEW_FP}")
            continue

        nets = {p.GetNumber(): p.GetNet() for p in old.Pads()}
        position = old.GetPosition()
        orientation = old.GetOrientation()
        layer = old.GetLayer()
        value = old.GetValue()

        new = pcbnew.FootprintLoad(LIB, NEW_FP)
        if new is None:
            sys.exit(f"Could not load {NEW_FP} from {LIB}")

        # FootprintLoad leaves the FPID without a library nickname; set the
        # full Library:Footprint id so the board records where it came from.
        new.SetFPID(pcbnew.LIB_ID("Package_SO", NEW_FP))
        new.SetReference(ref)
        new.SetValue(value)
        if layer != new.GetLayer():
            new.Flip(position, False)
        new.SetPosition(position)
        new.SetOrientation(orientation)

        missing = set(nets) - {p.GetNumber() for p in new.Pads()}
        if missing:
            sys.exit(f"{ref}: new footprint is missing pads {sorted(missing)}")

        for pad in new.Pads():
            net = nets.get(pad.GetNumber())
            if net is not None:
                pad.SetNet(net)

        board.Remove(old)
        board.Add(new)
        print(f"{ref}: swapped onto {NEW_FP}, {len(nets)} pad nets carried over")

    board.BuildListOfNets()
    pcbnew.ZONE_FILLER(board).Fill(board.Zones())
    pcbnew.SaveBoard(str(PCB), board)
    print("Saved.")


if __name__ == "__main__":
    main()
