# Remaining PCB work

Everything on this list is now applied. ERC 0, DRC 0 violations / 0 unconnected,
and the schematic and PCB agree on all 17 nets.

Applied by the scripts in `tools/`, using KiCad's `pcbnew` object model rather
than Konnect (whose PCB tools have no refdes rename, footprint swap, or
pad-to-net assignment, and whose `edit_component` reports field edits over IPC
as unimplemented):

- `fix_pcb.py` — unique ASCII refdes (TP1-TP7, J1, J2); R1/R2 1k -> 470R;
  TP6's floating pad tied to GND
- `swap_opto_footprint.py` — U2/U3 onto `SO-4_4.4x3.6mm_P2.54mm`
- `reroute_optos.py` — the three nudges that clear the taller pads
- `add_filter_caps.py` — C3/C4 at the divider midpoints

For Konnect's PCB tools to reach the board at all, a KiCad *editor* must own
`/tmp/kicad/api.sock`. The project manager claims it while holding no
documents, which makes every PCB call fail with `AS_UNHANDLED`:

```
osascript -e 'tell application "KiCad" to quit'
open -a "/Applications/KiCad/PCB Editor.app" beogram-esp32.kicad_pcb
```

## Notes worth keeping

The netclass clearance is 0.3mm, but U2/U3 carry a 0.2mm footprint override.
DRC reports the two differently, which matters when reading a violation.

The `U2_DRV` run necks to 0.3mm where it threads between U2.2 and the
DRV_PlayPause traces. It carries ~4.5mA into an opto LED.

C3/C4 sit at the divider midpoints rather than at the ESP32 pins, because the
noise they reject is picked up on the wires from the turntable — filtering
where those land catches it before it travels.

## MP1584 buck module (PS1)

The four hand-made 3.0 x 2.0mm pads under the "->MP1584->" note are now a real
component: `PS1`, on `beogram:MP1584_Module_Wired`, built by
`tools/add_buck_module.py`. Pad coordinates are unchanged, so nothing moved.

The module is off-board on flying leads, so the footprint is excluded from
position files and its courtyard hugs each pad rather than boxing the whole
area -- `create_footprint` drew a 20.5 x 11mm body courtyard, which C1 already
sits inside. `tools/fix_buck_footprint.py` corrects that and is re-runnable.

In the schematic PS1 uses `Connector_Generic:Conn_01x04`, the same placeholder
convention as U1. A proper named-pin symbol exists in `beogram.kicad_sym`
(IN+/IN-/OUT+/OUT-) if you want to swap it in Eeschema -- Konnect can only
place from stock libraries, so it could not be used directly.

This changed the power topology: VCC now comes from the buck, fed off
VBTN_12V, instead of the Beogram's 5.3V rail. The old TP1/TP4/TP6/TP7 test
points are gone, replaced by PS1's four pads.

## Not done

Nothing outstanding. Re-run `python3 tools/gen_bom.py` after any board change.
