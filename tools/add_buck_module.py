#!/usr/bin/env python3
"""Turn the four hand-made buck-converter pads into one real component.

TP4/TP6/TP7/TP1 were test points, resized by hand into 3.0 x 2.0mm wire pads,
with a "->MP1584->" note between them. Electrically that works, but the module
was invisible: nothing in the schematic showed where VCC came from, the
footprint name still claimed TestPoint_Pad_1.5x1.5mm, and the module never
reached the BOM.

This replaces those four footprints with a single PS1 on beogram:MP1584_Module_Wired,
whose pads land on the same coordinates. Nets are carried over, and the "12V"
/ "GND" / "5V" / "GND" silkscreen is re-added as board text at the same spots,
since those labels used to come from each test point's value field.

The module is off-board -- these are the pads its flying leads solder to -- so
the footprint is excluded from position files.

Run with KiCad's bundled Python, with the PCB editor closed.
"""

import sys
from pathlib import Path

import pcbnew

ROOT = Path(__file__).resolve().parent.parent
PCB = ROOT / "beogram-esp32.kicad_pcb"
LIB = str(ROOT / "beogram.pretty")
FP = "MP1584_Module_Wired"

MM = 1_000_000
CENTRE = (151.0, 109.466)

# old test point -> (new pad number, expected net)
MAPPING = {
    "TP4": ("1", "VBTN_12V"),
    "TP6": ("2", "GND"),
    "TP1": ("3", "VCC"),
    "TP7": ("4", "GND"),
}


def vec(x, y):
    return pcbnew.VECTOR2I(round(x * MM), round(y * MM))


def main():
    board = pcbnew.LoadBoard(str(PCB))

    if board.FindFootprintByReference("PS1") is not None:
        print("PS1 already present, nothing to do")
        return

    nets, labels = {}, []
    for ref, (pad_no, expected) in MAPPING.items():
        fp = board.FindFootprintByReference(ref)
        if fp is None:
            sys.exit(f"{ref} not found -- aborting without saving")
        pad = list(fp.Pads())[0]
        if pad.GetNetname() != expected:
            sys.exit(f"{ref} is on {pad.GetNetname()!r}, expected {expected!r} "
                     "-- aborting without saving")
        nets[pad_no] = pad.GetNet()

        val = fp.Value()
        labels.append((val.GetText(), val.GetPosition(), val.GetTextSize(),
                       val.GetTextThickness(), val.GetLayer()))

    for ref in MAPPING:
        board.Remove(board.FindFootprintByReference(ref))

    fp = pcbnew.FootprintLoad(LIB, FP)
    if fp is None:
        sys.exit(f"Could not load {FP} from {LIB}")
    fp.SetFPID(pcbnew.LIB_ID("beogram", FP))
    fp.SetReference("PS1")
    fp.SetValue("MP1584EN module")
    fp.SetPosition(vec(*CENTRE))
    # Off-board part: it belongs in the BOM but must not reach pick-and-place.
    fp.SetAttributes(fp.GetAttributes() | pcbnew.FP_EXCLUDE_FROM_POS_FILES)
    board.Add(fp)

    placed = board.FindFootprintByReference("PS1")
    for pad in placed.Pads():
        net = nets.get(pad.GetNumber())
        if net is None:
            sys.exit(f"No net captured for pad {pad.GetNumber()}")
        pad.SetNet(net)

    placed.Reference().SetLayer(pcbnew.F_Fab)
    placed.Reference().SetVisible(False)
    placed.Value().SetLayer(pcbnew.F_Fab)
    placed.Value().SetVisible(False)

    # Put the pad labels back as board text.
    for text, pos, size, thickness, layer in labels:
        t = pcbnew.PCB_TEXT(board)
        t.SetText(text)
        t.SetPosition(pos)
        t.SetTextSize(size)
        t.SetTextThickness(thickness)
        t.SetLayer(layer)
        board.Add(t)

    board.BuildListOfNets()
    pcbnew.ZONE_FILLER(board).Fill(board.Zones())
    pcbnew.SaveBoard(str(PCB), board)

    print("PS1 placed on beogram:MP1584_Module_Wired")
    for pad in sorted(placed.Pads(), key=lambda p: p.GetNumber()):
        p = pad.GetPosition()
        print(f"  pad {pad.GetNumber()} {pad.GetNetname():10} "
              f"({p.x/MM:.3f},{p.y/MM:.3f})")
    print(f"  re-added {len(labels)} silkscreen labels")


if __name__ == "__main__":
    main()
