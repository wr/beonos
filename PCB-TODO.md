# Remaining PCB work

## Done

Applied by `tools/fix_pcb.py`, using KiCad's `pcbnew` Python object model:

- All 20 footprints have unique, ASCII reference designators (TP1-TP7, J1, J2).
  Three were previously all called `GND`, and four used a `↘︎` glyph.
- R1, R2: 1k → 470R
- TP6's pad had no net; tied to GND and zones refilled

DRC is at 0 violations, 0 unconnected.

## Still open

Two items need interactive layout work. Both are blocked on the same thing:
this is a dense 25 x 64mm two-layer board with eight nets running its full
length between the connectors.

### Opto footprint swap — attempted, reverted

`tools/swap_opto_footprint.py` does the swap correctly: it carries position,
rotation, layer, and all four pad nets across, and leaves 0 unconnected. But
the resulting board has 8 DRC errors including a real short, so it is not
committed.

The cause is geometric. U2/U3 sit at -90°, so the new land's 2.0mm pad length
runs *vertically* on the board, straight into the vertical trace corridors.
Pads 1 and 2 are 2.54mm apart, so the gap between their edges drops to 0.54mm
— a 0.5mm trace cannot reach pad 1 through that gap at 0.2mm clearance. The
`U2_DRV`/`U3_DRV` traces would have to approach pad 1 from outside the
footprint, which means shifting several of the long parallel traces.

Worth knowing: the current `SO-4_4.4x4.3mm` land has the *correct pad centres*
(2.54mm pitch), so the leads do land on copper. What is lost is land area and
the toe fillet, which matters for reflow yield and inspection, not for whether
the part works. On a hand-built one-off it is a reasonable thing to live with.

Options, in increasing order of effort:

1. Leave it. Solderable, just a small land.
2. Rotate U2/U3 by 90° so the long pads run horizontally, then re-route.
3. Re-route the DRV traces around the outside of each opto.

### Filter caps C3/C4

Not placed. 10nF 0402, one per divider midpoint:

- C3: `IN_OnOff_3V3` (U1 pad 1) to GND
- C4: `IN_PlayPause_3V3` (U1 pad 4) to GND

They tame the 600kΩ Thevenin impedance feeding the GPIOs on wires that run past
a motor. 10nF gives a ~6ms time constant, well inside the firmware's 50ms
`delayed_on`/`delayed_off` filters. They are already in the schematic, so a
schematic sync will pull them in as unplaced footprints — see below.

Finding space and routing them wants a human eye on the board; the congestion
that defeated the footprint swap applies here too.

## Why this is a manual list

Konnect can read the board but cannot make any of these edits. `edit_component`
returns "Field edits via IPC are not yet supported", and the PCB toolset has no
reference-designator rename, no footprint swap, and no pad-to-net assignment.
Every change below has to come through the schematic sync or the GUI.

For Konnect's PCB tools to talk to the board at all, a KiCad **editor** must own
`/tmp/kicad/api.sock`. The project manager claims that socket while holding no
documents, which makes every PCB call fail with `AS_UNHANDLED`. Quit the manager
and launch the editor directly:

```
osascript -e 'tell application "KiCad" to quit'
open -a "/Applications/KiCad/PCB Editor.app" beogram-esp32.kicad_pcb
```

## Read this first

**Do not run Tools > Update PCB from Schematic (F8) with default options.**

The schematic was rebuilt from scratch, so every symbol has a new UUID. None of
them link to the existing footprints. With default options KiCad sees zero
matches, deletes all 20 footprints, and drops fresh unplaced ones on the board —
the layout and all routing are gone.

The rename in step 1 exists to make F8 safe. Do it first.

## 1. Rename the reference designators

Nine footprints need new refs. Three are currently all called `GND`, which is
what caused the floating pad in step 3. Select each, press `E`, set Reference:

| Current ref     | Position         | New ref |
| --------------- | ---------------- | ------- |
| `5V`            | 159.5, 113.75    | TP1     |
| `↘︎On/Off`      | 155.5, 67.5      | TP2     |
| `↘︎PlayPause`   | 146.01, 67.5     | TP3     |
| `12V`           | 142.5, 113.75    | TP4     |
| `↘︎GND`         | 142.1, 99.55     | TP5     |
| `GND`           | 142.5, 105.18    | TP6     |
| `GND`           | 159.5, 105.2     | TP7     |
| `TO BOARD`      | 159.75, 118.05   | J1      |
| `TO BUTTONS`    | 159.88, 72.6     | J2      |

Beyond fixing the duplicates, this drops the non-ASCII `↘︎` glyph, which is not
reliably rendered by fab silkscreen tooling.

## 2. Update PCB from Schematic — with re-link enabled

Tools > Update PCB from Schematic, and **tick "Re-link footprints to schematic
symbols based on their reference designators"**. With step 1 done, every
footprint matches by ref and the layout survives.

That single pass applies:

- R1, R2: 1k → 470R (~4.5mA through the opto LEDs instead of ~2.1mA)
- U2, U3: footprint → `Package_SO:SO-4_4.4x3.6mm_P2.54mm`
- C3, C4: added as new unplaced footprints
- The missing GND net on TP6 (see step 3)

## 3. Confirm TP6 picked up GND

The pad at 142.5, 105.18 currently has **no net assigned**. It is isolated
copper — the GND zone clears around it rather than connecting it. Step 2 should
assign GND from the schematic; verify it did.

## 4. Place and route C3, C4

10nF 0402, one per divider midpoint, as close to the U1 pad as they will go:

- C3: `IN_OnOff_3V3` (U1 pad 1) to GND
- C4: `IN_PlayPause_3V3` (U1 pad 4) to GND

They tame the 600kΩ Thevenin impedance feeding the GPIOs on wires that run
past a motor. 10nF gives a ~6ms time constant, well inside the firmware's 50ms
`delayed_on`/`delayed_off` filters.

## 5. Re-route U2/U3 and run DRC

The new opto land sits 0.3mm further out in X and the pads are a different
shape, so the short stubs into U2/U3 need a touch-up. Then DRC — it was at 0
violations before any of this.

## 6. Regenerate the BOM

```
python3 tools/gen_bom.py
```

It reads the PCB directly, so it picks up the 470R and the new caps.
