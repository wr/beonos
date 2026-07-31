#!/usr/bin/env python3
"""Put human-readable labels back on the test-point silkscreen.

Renaming the test points to TP1-TP7 gave them unique, valid references, but it
also left the silkscreen reading "TP1".."TP7". The descriptive text lives on
F.Fab, which is not manufactured. On a board where five flying leads get
hand-soldered to bare pads -- one of them +12V -- the silkscreen has to say
what each pad is.

So for the test points: show the value on the silkscreen, and move the
reference to F.Fab. Connectors keep their reference on silk, since their values
are long distributor part numbers.

Run with KiCad's bundled Python, with the PCB editor closed.
"""

from pathlib import Path

import pcbnew

ROOT = Path(__file__).resolve().parent.parent
PCB = ROOT / "beogram-esp32.kicad_pcb"

# Match the schematic's values, which read better on a board than VCC/VIN_12V.
LABELS = {
    "TP1": "5V",
    "TP2": "On/Off",
    "TP3": "PlayPause",
    "TP4": "12V",
    "TP5": "GND",
    "TP6": "GND",
    "TP7": "GND",
}


def main():
    board = pcbnew.LoadBoard(str(PCB))
    changed = []

    for ref, label in LABELS.items():
        fp = board.FindFootprintByReference(ref)
        if fp is None:
            raise SystemExit(f"{ref} not found -- aborting without saving")

        ref_text, val_text = fp.Reference(), fp.Value()

        if val_text.GetText() != label:
            changed.append(f"{ref}: value {val_text.GetText()!r} -> {label!r}")
            val_text.SetText(label)

        # Value takes the reference's spot on the silkscreen.
        val_text.SetLayer(ref_text.GetLayer())
        val_text.SetPosition(ref_text.GetPosition())
        val_text.SetTextSize(ref_text.GetTextSize())
        val_text.SetTextThickness(ref_text.GetTextThickness())
        val_text.SetVisible(True)

        ref_text.SetLayer(pcbnew.F_Fab)
        ref_text.SetVisible(False)
        changed.append(f"{ref}: silkscreen now reads {label!r}")

    pcbnew.SaveBoard(str(PCB), board)
    print(f"Applied {len(changed)} changes:")
    for line in changed:
        print("  " + line)


if __name__ == "__main__":
    main()
