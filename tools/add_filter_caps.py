#!/usr/bin/env python3
"""Place C3/C4, the divider filter caps, and connect them.

Run with KiCad's bundled Python, with the PCB editor closed.

The caps go at the divider midpoints rather than at the ESP32 pins. The noise
they exist to reject is picked up on the wires coming from the turntable, so
filtering where those wires land -- right below R6 and R8 -- catches it before
it travels anywhere. 10nF against the 600k Thevenin impedance is a ~6ms time
constant, well inside the firmware's 50ms delayed_on/delayed_off filters.

Each cap's signal pad sits directly under the existing divider run so the
connection is one short vertical trace. The ground pads connect through the
GND pour, which is refilled at the end.
"""

import sys
from pathlib import Path

import pcbnew

ROOT = Path(__file__).resolve().parent.parent
PCB = ROOT / "beogram-esp32.kicad_pcb"

LIB = "/Applications/KiCad/KiCad.app/Contents/SharedSupport/footprints/Capacitor_SMD.pretty"
FP = "C_0402_1005Metric"
MM = 1_000_000

# ref -> (centre, signal net, which pad carries the signal, trace end)
CAPS = {
    "C3": ((157.97, 70.30), "IN_OnOff_3V3", "2", (158.48, 68.82)),
    "C4": ((143.73, 70.30), "IN_PlayPause_3V3", "1", (143.22, 69.50)),
}


def vec(x, y):
    return pcbnew.VECTOR2I(round(x * MM), round(y * MM))


def main():
    board = pcbnew.LoadBoard(str(PCB))

    gnd = board.FindNet("GND")
    if gnd is None:
        sys.exit("No GND net -- aborting")

    for ref, (centre, signal_net, signal_pad, trace_end) in CAPS.items():
        if board.FindFootprintByReference(ref) is not None:
            print(f"{ref} already placed, skipping")
            continue

        net = board.FindNet(signal_net)
        if net is None:
            sys.exit(f"No {signal_net} net -- aborting")

        fp = pcbnew.FootprintLoad(LIB, FP)
        if fp is None:
            sys.exit(f"Could not load {FP} from {LIB}")

        fp.SetFPID(pcbnew.LIB_ID("Capacitor_SMD", FP))
        fp.SetReference(ref)
        fp.SetValue("10nF")
        fp.SetPosition(vec(*centre))
        board.Add(fp)

        for pad in fp.Pads():
            pad.SetNet(net if pad.GetNumber() == signal_pad else gnd)

        placed = board.FindFootprintByReference(ref)
        sig = next(p for p in placed.Pads() if p.GetNumber() == signal_pad)
        start = sig.GetPosition()

        track = pcbnew.PCB_TRACK(board)
        track.SetStart(start)
        track.SetEnd(vec(*trace_end))
        track.SetWidth(round(0.5 * MM))
        track.SetLayer(pcbnew.F_Cu)
        track.SetNet(net)
        board.Add(track)

        print(f"{ref} at {centre}: pad {signal_pad} -> {signal_net} "
              f"({start.x/MM:.2f},{start.y/MM:.2f}) -> {trace_end}, other pad -> GND")

    board.BuildListOfNets()
    pcbnew.ZONE_FILLER(board).Fill(board.Zones())
    pcbnew.SaveBoard(str(PCB), board)
    print("Saved.")


if __name__ == "__main__":
    main()
