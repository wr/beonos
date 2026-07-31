#!/usr/bin/env python3
"""Tighten the MP1584 footprint's courtyard to the pads.

create_footprint drew one courtyard around the whole pad envelope -- 20.5 x
11mm -- plus a silkscreen box of the same size. Both describe a component body
sitting on the board, and there isn't one: the module hangs off on flying
leads, and these are only the pads those leads solder to. C1 already lives
inside that rectangle, which is what the courtyard overlap was reporting.

So: drop the body courtyard and the silk box, and give each pad its own small
courtyard instead. The DRC check stays meaningful -- nothing may sit on the
pads -- without claiming space the module does not occupy.

Operates on PS1 as placed on the board, because SWIG does not expose pads
usefully on a footprint loaded straight from a library. The corrected footprint
is written back to the library so future placements match.

Run with KiCad's bundled Python, with the PCB editor closed.
"""

from pathlib import Path

import pcbnew

ROOT = Path(__file__).resolve().parent.parent
PCB = ROOT / "beogram-esp32.kicad_pcb"
LIB = str(ROOT / "beogram.pretty")

MM = 1_000_000
MARGIN = 0.25  # courtyard clearance around each pad, mm


def main():
    board = pcbnew.LoadBoard(str(PCB))
    fp = board.FindFootprintByReference("PS1")
    if fp is None:
        raise SystemExit("PS1 not on the board -- run add_buck_module.py first")

    # Read pad geometry up front: calling Remove() below leaves the SWIG proxy
    # unable to iterate Pads() afterwards.
    pad_boxes = [(p.GetPosition(), p.GetSize()) for p in fp.Pads()]

    # Clear every courtyard rectangle, including ones a previous run added, so
    # this is safe to re-run. The silkscreen box goes too -- the pad labels are
    # board text and the "->MP1584->" note is hand-placed.
    removed = 0
    for shape in list(fp.GraphicalItems()):
        if shape.GetClass() != "PCB_SHAPE" or shape.GetShape() != pcbnew.SHAPE_T_RECT:
            continue
        if shape.GetLayer() in (pcbnew.F_CrtYd, pcbnew.B_CrtYd, pcbnew.F_SilkS):
            fp.Remove(shape)
            removed += 1

    added = 0
    for pos, size in pad_boxes:
        hx = size.x / 2 + MARGIN * MM
        hy = size.y / 2 + MARGIN * MM
        rect = pcbnew.PCB_SHAPE(fp, pcbnew.SHAPE_T_RECT)
        rect.SetStart(pcbnew.VECTOR2I(int(pos.x - hx), int(pos.y - hy)))
        rect.SetEnd(pcbnew.VECTOR2I(int(pos.x + hx), int(pos.y + hy)))
        rect.SetLayer(pcbnew.F_CrtYd)
        rect.SetWidth(round(0.05 * MM))
        rect.SetFilled(False)
        fp.Add(rect)
        added += 1

    pcbnew.ZONE_FILLER(board).Fill(board.Zones())
    pcbnew.SaveBoard(str(PCB), board)

    # Keep the library in step with what the board now has.
    pcbnew.FootprintSave(LIB, fp)

    print(f"Removed {removed} body shapes, added {added} per-pad courtyards")


if __name__ == "__main__":
    main()
