<img width="492" height="184" alt="beonos" src="https://github.com/user-attachments/assets/0bac70b3-670b-4b0c-a56c-cbdf7ba0482e" />

# beonos: Sonos integration for vintage Bang & Olufsen turntables

A small ESP32-C6 board that intercepts the Beogram RX2 (and likely other) turntable's button controls, senses play/pause status, and connects them to a Sonos speaker over UPnP.

## What it does

- **Turntable → Sonos**: when you start the turntable, the speaker switches to Line-In and starts playing. When you pause or turn off the turntable, the speaker pauses.
- **Sonos → turntable**: selecting Line-In on the Sonos starts the turntable (presses Cue). Switching away or pausing on the speaker stops the turntable. If you start Sonos Line-In and there's no record on the platter, the firmware notices and pauses Sonos.

## Hardware

- **MCU**: Seeed Studio XIAO ESP32-C6 (castellated edge-mount module)
- **Button drivers**: 2× LTV-356T optocouplers
- **Level shifters**: 2× 1M/1.5M voltage dividers — drop the COP410's 5.3V logic to ~3.18V for the ESP's 3.3V GPIOs. The values are deliberately high so the divider barely loads the COP410's weak CMOS outputs, and so any overvoltage is current-limited into the ESP's ESD clamps.
- **Decoupling**: 10µF + 100nF on the 5.3V rail (tapped from the Beogram's main board)
- **Input filtering**: 10nF from each divider midpoint to ground, sitting right below R6/R8. The dividers present ~600kΩ to the ESP's inputs on wires that run past a motor, and the caps give a ~6ms time constant — well inside the firmware's 50 ms debounce.
- **Connectors**: two 1×8 horizontal headers (one socket, one header) for in-line splice into the Beogram's existing button controller cable. Pins 1, 2, 4, 6, 7, 8 pass straight through; pins 3 and 5 (CUE and PLAYPAUSE) also land on the optocoupler emitters so the ESP can switch +12V onto them alongside the real buttons.
- **Flying leads**: the 5.3V supply, ground, the +12V button rail, and the two COP410 sense signals are picked up on SMD test pads, not the headers. The pads are TP1–TP7 in the schematic, but the silkscreen is labelled by function — `5V`, `On/Off`, `PlayPause`, `12V`, `GND` — so solder by the printed label, not the designator.

PCB sources: `beogram-esp32.kicad_pcb`, `.kicad_sch`, `.kicad_pro`. BOM in [`bom.csv`](bom.csv).

### Pin map

| XIAO pin | Pad | GPIO   | Net               | Purpose                                          |
| -------- | --- | ------ | ----------------- | ------------------------------------------------ |
| D0       | 1   | GPIO0  | IN_OnOff_3V3      | COP410 pin 11 → ESP. LOW = arm in (turntable on) |
| D3       | 4   | GPIO21 | IN_PlayPause_3V3  | COP410 pin 12 → ESP. HIGH = playing              |
| D8       | 9   | GPIO19 | DRV_PlayPause     | ESP → U2 opto LED. Pulse 500 ms to "press" PLAYPAUSE |
| D10      | 11  | GPIO18 | DRV_Cue           | ESP → U3 opto LED. Pulse 500 ms to "press" CUE   |
| 5V       | 14  | —      | VCC (5.3V in)     | Power in from Beogram's 5.3V rail                |
| GND      | 13  | —      | GND               | Common ground                                    |

D6/D7 (GPIO16/17) are UART0 and are deliberately left unconnected, so the ROM
bootloader's serial output can't reach an optocoupler.

## Firmware (ESPHome)

```bash
pip install esphome
cp secrets.yaml.example secrets.yaml
# fill in WiFi creds in secrets.yaml
esphome run beogram-rx2.yaml
```

First flash needs USB-C; subsequent updates happen OTA.

### Configuring the Sonos speaker

After it joins WiFi, the device shows up in Home Assistant. Set the **"Sonos IP"** entity to your speaker's IP. That's the only setup.

### Exposed entities

| Entity                    | Type           | Notes                                   |
| ------------------------- | -------------- | --------------------------------------- |
| Turntable On              | binary_sensor  | true = arm in / playing position        |
| Turntable Playing         | binary_sensor  | true = motor on, arm down               |
| Sonos Playing             | binary_sensor  | derived from polled transport state     |
| Sonos On Line-In          | binary_sensor  | true if speaker is on line-in           |
| Sonos Transport State     | text_sensor    | PLAYING / PAUSED_PLAYBACK / STOPPED     |
| Sonos Current URI         | text_sensor    | what the speaker is currently playing   |
| Sonos IP                  | text (config)  | edit in HA, persists across reboots     |
| Press Play/Pause          | button         | manually fire the turntable's PLAYPAUSE |
| Press Cue                 | button         | manually fire the turntable's CUE       |
| Sonos Play / Pause        | buttons        | manual SOAP commands                    |
| Sonos Switch to Line-In   | button         | manual SOAP command                     |
| WiFi Signal               | sensor         | dBm, 60s update                         |

## Repo layout

```
beogram-esp32.kicad_pro     KiCad project
beogram-esp32.kicad_sch     Schematic
beogram-esp32.kicad_pcb     PCB layout
fp-lib-table                Registers New_XIAO_Series_Footprints
beogram-rx2.yaml            ESPHome firmware config
secrets.yaml.example        Template for WiFi/OTA/API creds
bom.csv                     Bill of materials — generated, do not hand-edit
tools/                      Board scripts (see PCB-TODO.md)
PCB-TODO.md                 Board change log and gotchas
```

`bom.csv` is generated from the PCB, so it can't drift from what gets built:

```bash
python3 tools/gen_bom.py
```

The other scripts in `tools/` need KiCad's bundled Python, which is the only one
carrying the `pcbnew` module:

```bash
/Applications/KiCad/KiCad.app/Contents/Frameworks/Python.framework/Versions/Current/bin/python3 tools/<script>.py
```
