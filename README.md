<img width="492" height="184" alt="BEONOS CONTROLLER" src="https://github.com/user-attachments/assets/8dabba41-4358-4a6c-9672-bfffa8589348" />

# beonos: Sonos integration for vintage Bang & Olufsen turntables

A small ESP32-C6 board that intercepts the Beogram RX2 (and likely other) turntable's button controls, senses play/pause status, and connects them to a Sonos speaker over UPnP.

## What it does

- **Turntable → Sonos**: when you start the turntable, the speaker switches to Line-In and starts playing. When you pause or turn off the turntable, the speaker pauses.
- **Sonos → turntable**: selecting Line-In on the Sonos starts the turntable (presses Cue). Switching away or pausing on the speaker stops the turntable. If you start Sonos Line-In and there's no record on the platter, the firmware notices and pauses Sonos.

## Hardware

- **MCU**: Seeed Studio XIAO ESP32-C6 (castellated edge-mount module)
- **Button drivers**: 2× LTV-356T optocouplers
- **Level shifters**: 2× 10k/15k voltage dividers — drop the COP410's 5.3V logic to ~3.18V for the ESP's 3.3V GPIOs
- **Decoupling**: 10µF + 100nF on the 5.3V rail (tapped from the Beogram's main board)
- **Connectors**: two 1×8 horizontal headers (one socket, one header) for in-line splice into the Beogram's existing button controller cable.

PCB sources: `beogram-esp32.kicad_pcb`, `.kicad_sch`, `.kicad_pro`. Gerbers in [`gerbers/`](gerbers/). BOM in [`bom.csv`](bom.csv).

### Pin map

| XIAO pin | GPIO   | Net               | Purpose                                          |
| -------- | ------ | ----------------- | ------------------------------------------------ |
| D0       | GPIO0  | IN_OnOff_3V3      | COP410 pin 11 → ESP. LOW = arm in (turntable on) |
| D1       | GPIO1  | IN_PlayPause_3V3  | COP410 pin 12 → ESP. HIGH = playing              |
| D6       | GPIO16 | DRV_PlayPause    | ESP → U2 opto LED. Pulse 500 ms to "press" PLAYPAUSE |
| D7       | GPIO17 | DRV_Cue           | ESP → U3 opto LED. Pulse 500 ms to "press" CUE   |
| 5V       | —      | VIN_5V3           | Power in from Beogram's 5.3V rail                |
| GND      | —      | GND               | Common ground                                    |

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
beogram-rx2.yaml            ESPHome firmware config
secrets.yaml.example        Template for WiFi/OTA/API creds
gerbers/                    Manufacturing files (export from KiCad)
bom.csv                     Bill of materials
```
