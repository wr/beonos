# Beonos firmware

This page installs the Beonos firmware on a Beonos Controller over USB, from Chrome or Edge on a computer. Pre-soldered kits already have it, so you only need this for a board you built yourself, or to start a board over.

1. Connect the board's XIAO ESP32-C6 to your computer with a USB-C cable.
2. Click **Connect**, choose the board's serial port, and follow the steps.
3. When it's done, the board opens a Wi-Fi network called **Beonos Setup** followed by a short code. Join it from your phone to put the board on your Wi-Fi.

<esp-web-install-button manifest="firmware/beonos.manifest.json"></esp-web-install-button>

After that, the board updates itself from here. Setup, speaker choice and Home Assistant are covered in the [README](https://github.com/wr/beonos#firmware).

<script type="module" src="https://unpkg.com/esp-web-tools@10/dist/web/install-button.js?module"></script>
