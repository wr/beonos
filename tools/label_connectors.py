#!/usr/bin/env python3
"""Put "TO BOARD" / "TO KEYBOARD" back on the connector silkscreen.

Those strings used to be the reference designators themselves, which is why
renaming to J1/J2 lost them. The rename had to happen -- the old references
collided with each other and with the test points -- so the descriptive text
comes back as its own silkscreen item, and J1/J2 move to B.Fab where they
still serve assembly without cluttering the board.

The text is owned by the footprint, so it travels if the connector moves.

Run with KiCad's bundled Python, with the PCB editor closed.
"""

from pathlib import Path

import pcbnew

ROOT = Path(__file__).resolve().parent.parent
PCB = ROOT / "beogram-esp32.kicad_pcb"

MM = 1_000_000
LABELS = {"J1": "TO BOARD", "J2": "TO KEYBOARD"}
SIZE = 1.5
THICKNESS = 0.25


def main():
    board = pcbnew.LoadBoard(str(PCB))
    changed = []

    for ref, label in LABELS.items():
        fp = board.FindFootprintByReference(ref)
        if fp is None:
            raise SystemExit(f"{ref} not found -- aborting without saving")

        # Drop any label a previous run added, so this is safe to re-run.
        for item in list(fp.GraphicalItems()):
            if item.GetClass() == "PCB_TEXT" and item.GetText() in LABELS.values():
                fp.Remove(item)

        ref_text = fp.Reference()
        pos = ref_text.GetPosition()
        mirrored = ref_text.IsMirrored()
        angle = ref_text.GetTextAngle()

        text = pcbnew.PCB_TEXT(fp)
        text.SetText(label)
        text.SetPosition(pos)
        text.SetLayer(ref_text.GetLayer())
        text.SetMirrored(mirrored)
        text.SetTextAngle(angle)
        text.SetTextSize(pcbnew.VECTOR2I(round(SIZE * MM), round(SIZE * MM)))
        text.SetTextThickness(round(THICKNESS * MM))
        fp.Add(text)

        ref_text.SetLayer(pcbnew.B_Fab)
        ref_text.SetVisible(False)

        changed.append(f"{ref}: silkscreen reads {label!r}, designator moved to B.Fab")

    pcbnew.SaveBoard(str(PCB), board)
    print(f"Applied {len(changed)} changes:")
    for line in changed:
        print("  " + line)


if __name__ == "__main__":
    main()
