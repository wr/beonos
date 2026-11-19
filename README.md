<p align="center">
  <img width="492" alt="Beonos logo" src="https://github.com/user-attachments/assets/39ba7ad3-26b8-4cf6-aa79-a4e7724b0d99" />
</p>
<h1 align="center">Beonos Controller</h1>

<p align="center">
  <strong>Control your vintage Bang & Olufsen Beogram turntable from Home Assistant or Sonos.</strong>
</p>

<p align="center">
  <a href="#what-is-it">What is it?</a> ⬪
  <a href="#buy-a-board">Buy a board</a> ⬪
  <a href="#which-turntables">Which turntables</a> ⬪
  <a href="#hardware">Hardware</a> ⬪
  <a href="#firmware">Firmware</a> ⬪
  <a href="#repo-layout">Repo layout</a> ⬪
  <a href="#license">License</a>
</p>

<p align="center">
  <a href="https://youtu.be/ydfJdppEwTE"><img width="600" alt="YouTube player" src="https://github.com/user-attachments/assets/db79c0d7-7f2f-469a-bbb2-3c8fcd094ffc" /></a>
</p>

<p align="center">
  I recorded a full build video that <a href="https://youtu.be/ydfJdppEwTE">you can watch here</a>.
</p>

---

## Buy a board

Get it as a kit, pre-soldered or not, from the [Wells Workshop shop](https://shop.wells.ee/products/beonos-kit/?ref=gh-beonos).

Or order your own Beonos Controller from [PCBway](https://www.pcbway.com/project/shareproject/Beonos_Controller_80184a3d.html). You can [use my referral code](https://pcbway.com/g/AsfKU9) to get $5 off your order, if you want. In my experience they're usually pretty fast and high quality, especially for the price.

## What is it?

A small ESP32-C6-powered custom PCB that splices into a Beogram's keyboard cable, reads what the turntable is doing, and sends play/pause/stop commands. The result is a 'smart' vintage turntable that behaves like a digital Sonos source.

- **Turntable → Sonos**: start the turntable and the speaker switches to Line-In and plays. Pause or switch off, and the speaker pauses.
- **Sonos → turntable**: pick Line-In on the speaker and the turntable starts (it presses Cue). Switch away or pause, and the turntable stops. If there's no record on the platter, the firmware notices and pauses Sonos rather than leaving it playing silence.

The board splices in-line, so nothing about the turntable is modified permanently — unplug the two headers and it's stock again.

## Which turntables

The Beogram 1800 family — the last generation of radial-arm Beograms — all share one control board, so the splice is the same across them:

| Model | Years | Status |
| ----- | ----- | ------ |
| Beogram RX 2 | 1985–90 | Built and tested against this |
| Beogram 1800 | 1983–84 | Confirmed from service manuals |
| Beogram 2000 | 1983 | Confirmed from service manuals |
| Beogram 5000 | 1984 | Confirmed from service manuals |
| Beogram RX | 1984 | Very likely, not yet verified |

The RX-2 service manual is published as a supplement to the Beogram 1800/2000/5000 manual, and the three share the same power supply (PCB 8005076) and control board (PCB 8005117). Their keyboards are the same two sub-boards — 8005070 for PLAY/STOP and 8005109 for 45/CUE/33 — landing on the same P7 pins, which is what this board splices into. The RX is unverified only because no manual was to hand; it replaced the 1800 and differs from the RX 2 mainly in being supplied with a pickup.

**Not compatible.** The Beogram 2200 looks close but wires its speed switches straight to the control board alongside indicator lamps and a motor relay, with no separate keyboard board and no CUE or PLAY/STOP lines to intercept. Tangential-arm Beograms (4002, 8000, 6500, 7000, 9000, 5005 and relatives) use a different arm and control system entirely, and pre-1983 radial decks predate this microcomputer control.

To identify an unknown deck, look for a control PCB stamped **8005117**, a 24-pin MCU marked **B040680** / **ETL 5410N**, and a keyboard on its own cable whose buttons switch +12V.

## Hardware

- **MCU**: Seeed Studio XIAO ESP32-C6 (castellated edge-mount module)
- **Button drivers**: 2× LTV-356T optocouplers, switching the turntable's +12V onto the CUE and PLAYPAUSE lines exactly as the real buttons do
- **Level shifters**: 2× 1M/1.5M dividers dropping the COP410's 5.3V logic to ~3.18V for the ESP's 3.3V inputs. The values are deliberately high so the divider barely loads the COP410's weak CMOS outputs, and any overvoltage is current-limited into the ESP's ESD clamps.
- **Power**: an off-board MP1584EN buck module (PS1) steps the +12V button rail down to 5V, so the board draws everything from the button cable and never taps the 5.3V supply. The module hangs on flying leads; the board carries four wire pads for it.
- **Decoupling**: 10µF + 100nF on the buck's 5V output
- **Input filtering**: 10nF from each divider midpoint to ground, below R6/R8. The dividers present ~600kΩ to the ESP on wires that run past a motor; the caps give a ~6ms time constant, well inside the firmware's 50 ms debounce.
- **Connectors**: two 1×8 horizontal headers for the in-line splice — J1 (socket, silkscreened `TO BOARD`) and J2 (header, silkscreened `TO KEYBOARD`). Pins 1, 2, 4, 6, 7, 8 pass straight through; pins 3 and 5 also land on the optocoupler emitters.
- **Flying leads**: the two COP410 sense signals and a ground reference arrive on test pads (TP2, TP3, TP5); four more pads go out to the buck module. Everything is silkscreened by function — `On-Off IN`, `PlayPause IN`, `GND IN`, `12V`, `5V`, `GND` — so solder by the printed label, not the designator.

Board is 25 × 64 mm, two layers. Sources are `beogram-esp32.kicad_pcb` / `.kicad_sch` / `.kicad_pro`, with the BOM in [`bom.csv`](bom.csv).

### Pin map

| XIAO pin | Pad | GPIO   | Net               | Purpose                                              |
| -------- | --- | ------ | ----------------- | ---------------------------------------------------- |
| D0       | 1   | GPIO0  | IN_OnOff_3V3      | COP410 pin 11 → ESP. LOW = arm in (turntable on)     |
| D3       | 4   | GPIO21 | IN_PlayPause_3V3  | COP410 pin 12 → ESP. HIGH = playing                  |
| D8       | 9   | GPIO19 | DRV_PlayPause     | ESP → U2 opto LED. Pulse 500 ms to "press" PLAYPAUSE |
| D10      | 11  | GPIO18 | DRV_Cue           | ESP → U3 opto LED. Pulse 500 ms to "press" CUE       |
| 5V       | 14  | —      | VCC               | 5V in from the MP1584 buck (PS1)                     |
| GND      | 13  | —      | GND               | Common ground                                        |

D6/D7 (GPIO16/17) are UART0 and are left unconnected on purpose, so the ROM bootloader's serial output can't reach an optocoupler on reset.

## Firmware

Every pre-soldered kit comes with the firmware installed. It's built on [ESPHome](https://esphome.io). You need a Sonos speaker with a line-in; Home Assistant is optional.

### Setting it up

1. Wire the board in and switch the turntable on.
2. On your phone, join the Wi-Fi network **Beonos Setup** followed by a six-character code. A setup page opens. If it doesn't, open `http://192.168.4.1`.
3. Choose your Wi-Fi network, type its password and tap **Connect**. Beonos needs a 2.4 GHz network. Leave **Install updates automatically** ticked unless you'd rather install updates yourself.
4. The page tells you when Beonos has joined, or what went wrong if it couldn't. It also shows the board's own address, like `http://beonos-1a2b3c.local`. Note it down: that's where its settings live.

Beonos then looks for a Sonos speaker with a line-in and uses it. If you have more than one, choose yours under **Sonos speaker** on the board's page. If it finds none, check that UPnP is on in the Sonos app (Account > Privacy & Security).

**The setup network only stays open for 15 minutes** after you plug the turntable in, once Beonos is on your Wi-Fi, so nobody nearby can change its Wi-Fi later. To move it to a new Wi-Fi network, unplug the turntable and plug it back in somewhere it can't reach the old one, or press **Reset** on the board's page.

### The board's page

Open the address from the setup page in a browser on the same Wi-Fi. If `.local` addresses don't work on your network, use the board's IP address from your router instead. The page shows what the turntable and speaker are doing, and has the speaker choice, the update settings and a **Reset** button.

**Updates** come from this repo's releases. Beonos checks for them by itself and installs them only while the turntable is off, so an update never interrupts a record. Turn off **Install updates automatically** to install them yourself from the page instead.

**Reset** wipes the Wi-Fi details, the speaker choice and the update setting, and Beonos goes back to setup.

### Home Assistant

Once Beonos is on your Wi-Fi, Home Assistant should find it as a new device. If it doesn't, add the ESPHome integration with the board's address. Either way, Home Assistant sets up an encrypted connection by itself. You can then use its sensors and buttons in automations.

To change the firmware itself, adopt the board in the ESPHome add-on. After that, the add-on builds and updates it, and it stops taking updates from this repo.

### Installing it on a board you built

Use the installer at [wr.github.io/beonos](https://wr.github.io/beonos/) from Chrome or Edge, with the board on USB-C. Or build it yourself with ESPHome 2026.11.0:

```bash
pip install esphome==2026.11.0
esphome run beonos.factory.yaml
```

`tests/run.sh` runs the component's tests on a computer (it needs a C++17 compiler).

### Entities

| Entity                         | Type          | Notes                                          |
| ------------------------------ | ------------- | ---------------------------------------------- |
| Turntable On                   | binary_sensor | on = arm in / playing position                 |
| Turntable Playing              | binary_sensor | on = motor on, arm down                        |
| Sonos Playing                  | binary_sensor | the speaker is playing                         |
| Sonos On Line-In               | binary_sensor | the speaker is playing its line-in             |
| Sonos status                   | text_sensor   | which speaker Beonos is using, or what's wrong |
| Sonos transport state          | text_sensor   | PLAYING / PAUSED_PLAYBACK / STOPPED            |
| Sonos current URI              | text_sensor   | what the speaker is playing                    |
| Sonos speaker                  | select        | choose the speaker when there's more than one  |
| Speaker address (advanced)     | text          | only if the search can't find your speaker     |
| Install updates automatically  | switch        | on by default                                  |
| Firmware                       | update        | install an update by hand                      |
| Search for speakers            | button        | search again now                               |
| Press Play-Pause / Press Cue   | buttons       | press the turntable's own buttons              |
| Sonos Play / Pause             | buttons       | send the speaker a command                     |
| Sonos Switch to Line-In        | button        | switch the speaker to its line-in              |
| Reset                          | button        | back to setup, with settings wiped             |
| WiFi Signal                    | sensor        | dBm                                            |

## Repo layout

```
beogram-esp32.kicad_pro     KiCad project
beogram-esp32.kicad_sch     Schematic
beogram-esp32.kicad_pcb     PCB layout
fp-lib-table                Registers the project footprint libraries
sym-lib-table               Registers the project symbol library
beogram.pretty/             Project footprints (MP1584 wire pads)
beogram.kicad_sym           Project symbols
New_XIAO_Series_Footprints/ Seeed's XIAO footprint library (vendor)
bom.csv                     Bill of materials
beonos.yaml                 Firmware: what the board does
beonos.factory.yaml         Firmware: what ships on kits (setup, updates, Reset)
components/beonos/          Firmware: Sonos search and control, setup page
tests/                      Tests for the component's parsing and setup logic
static/                     The installer site at wr.github.io/beonos
.github/workflows/          Builds, releases and the installer site
bang-olufsen_beogram_rx2_sch.pdf   RX 2 service-manual schematic, for reference
```

## Credits

ESPHome, the Seeed Studio XIAO ESP32-C6, KiCad, and Bang & Olufsen's engineers, who documented the RX2 well enough to reverse a splice point out of it forty years later.

## Donate

While beonos is free and open source, donations are deeply appreciated, and make ongoing development and support possible.
[Donate now](https://www.buymeacoffee.com/wellsworkshop)

## License

The hardware is [CC BY-NC-SA 4.0](LICENSE) and the firmware is [GPL-3.0-or-later](LICENSE-GPL-3.0.txt). See [LICENSE](LICENSE) for which files are which. © 2026 Wells Riley.
