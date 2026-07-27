# Remaining PCB work

The schematic is now correct and ERC-clean. The PCB has not been touched yet.
These are the changes still owed to the board.

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
