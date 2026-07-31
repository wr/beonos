#!/usr/bin/env python3
"""Re-route around the enlarged LTV-356T lands.

Run after tools/swap_opto_footprint.py, with the PCB editor closed.

The correct land makes each pad 2.0mm tall in board Y instead of 1.2mm, so the
GND pads (U2.2, U3.2) now reach up to Y=94.65 where the U2_DRV/U3_DRV runs used
to pass at Y=94.39 and Y=94.65. And U2.4 grows down to Y=102.95, into the
BTN_PlayPause via at Y=103.10.

Contrary to a first reading, the pads did not close up the gap between pad 1 and
pad 2 -- at -90 degrees those sit side by side in X, 2.54mm apart, so there is
1.9mm of clear space between them. The pads grew in Y only. So this is three
small nudges rather than a re-placement:

  - drop both DRV horizontals to Y=94.15 (0.25mm clear of the pad tops)
  - move the BTN_PlayPause via 0.5mm down to Y=103.60, and carry the F.Cu stub
    and both B.Cu verticals with it
"""

import sys
from pathlib import Path

import pcbnew

ROOT = Path(__file__).resolve().parent.parent
PCB = ROOT / "beogram-esp32.kicad_pcb"

MM = 1_000_000
TOL = 3000  # nm


def pt(x, y):
    return (round(x * MM), round(y * MM))


# (net, old_start, old_end) -> (new_start, new_end)
TRACK_EDITS = {
    ("U3_DRV", pt(157.870, 95.800), pt(156.720, 94.650)):
        (pt(157.870, 95.800), pt(156.300, 94.150)),
    ("U3_DRV", pt(156.720, 94.650), pt(154.612, 94.650)):
        (pt(156.300, 94.150), pt(154.612, 94.150)),
    # Drop to R2.2's row further right, then come in horizontally, so the run
    # stays clear of R2.1 (DRV_Cue) which sits directly above R2.2.
    ("U3_DRV", pt(154.612, 94.650), pt(152.872, 96.390)):
        (pt(154.612, 94.150), pt(153.700, 96.390)),
    ("U3_DRV", pt(152.872, 96.390), pt(152.600, 96.390)):
        (pt(153.700, 96.390), pt(152.600, 96.390)),

    ("U2_DRV", pt(150.370, 95.800), pt(148.960, 94.390)):
        (pt(150.370, 95.800), pt(148.960, 94.150)),
    # This pair threads a genuinely tight corridor -- U2.2's pad above right,
    # the DRV_PlayPause runs and R1.1 below left. Neck to 0.3mm through it,
    # which buys 0.1mm of clearance on both sides. It carries ~4.5mA into an
    # opto LED, so 0.5mm was never needed here.
    ("U2_DRV", pt(148.960, 94.390), pt(147.372, 94.390)):
        (pt(148.960, 94.150), pt(147.372, 94.150), 0.3),
    ("U2_DRV", pt(147.372, 94.390), pt(145.372, 96.390)):
        (pt(147.372, 94.150), pt(145.372, 96.390), 0.3),

    ("BTN_PlayPause", pt(149.720, 103.100), pt(149.130, 103.100)):
        (pt(149.720, 103.600), pt(149.130, 103.600)),
    ("BTN_PlayPause", pt(149.130, 103.100), pt(147.830, 101.800)):
        (pt(149.130, 103.600), pt(147.830, 102.300)),
    ("BTN_PlayPause", pt(149.720, 103.100), pt(149.720, 117.920)):
        (pt(149.720, 103.600), pt(149.720, 117.920)),
    ("BTN_PlayPause", pt(149.720, 72.600), pt(149.720, 103.100)):
        (pt(149.720, 72.600), pt(149.720, 103.600)),
}

VIA_EDITS = {("BTN_PlayPause", pt(149.720, 103.100)): pt(149.720, 103.600)}


def near(a, b):
    return abs(a[0] - b[0]) <= TOL and abs(a[1] - b[1]) <= TOL


def main():
    board = pcbnew.LoadBoard(str(PCB))

    if str(board.FindFootprintByReference("U2").GetFPID().GetLibItemName()) \
            != "SO-4_4.4x3.6mm_P2.54mm":
        sys.exit("Run swap_opto_footprint.py first -- U2 is not on the new land")

    applied = []
    for track in board.GetTracks():
        net = track.GetNetname()

        if track.Type() == pcbnew.PCB_VIA_T:
            pos = track.GetPosition()
            for (n, old), new in VIA_EDITS.items():
                if n == net and near((pos.x, pos.y), old):
                    track.SetPosition(pcbnew.VECTOR2I(*new))
                    applied.append(f"via  {net} -> ({new[0]/MM:.3f},{new[1]/MM:.3f})")
            continue

        s, e = track.GetStart(), track.GetEnd()
        for (n, o_s, o_e), spec in TRACK_EDITS.items():
            if n != net:
                continue
            if near((s.x, s.y), o_s) and near((e.x, e.y), o_e):
                n_s, n_e = spec[0], spec[1]
                width = spec[2] if len(spec) > 2 else None
                track.SetStart(pcbnew.VECTOR2I(*n_s))
                track.SetEnd(pcbnew.VECTOR2I(*n_e))
                note = ""
                if width is not None:
                    track.SetWidth(round(width * MM))
                    note = f" w={width}"
                applied.append(
                    f"trk  {net} ({n_s[0]/MM:.3f},{n_s[1]/MM:.3f})"
                    f"->({n_e[0]/MM:.3f},{n_e[1]/MM:.3f}){note}")
                break

    expected = len(TRACK_EDITS) + len(VIA_EDITS)
    if len(applied) != expected:
        sys.exit(f"Matched {len(applied)} of {expected} edits -- "
                 "geometry is not what was expected, not saving")

    board.BuildListOfNets()
    pcbnew.ZONE_FILLER(board).Fill(board.Zones())
    pcbnew.SaveBoard(str(PCB), board)

    print(f"Applied {len(applied)} edits:")
    for line in applied:
        print("  " + line)


if __name__ == "__main__":
    main()
