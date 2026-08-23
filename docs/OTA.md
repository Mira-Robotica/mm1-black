# Firmware update

## On device (SETUP → About / WiFi)

Shows **FW_VERSION** and a QR code to:

**https://verlab.github.io/mm1-black/**

P4 QR opens `?board=p4`. Wi-Fi defaults **Off**. Join an access point
to check GitHub Pages for a newer release and install it on the device.

## Installer (PC)

1. Open the **[firmware installer](https://verlab.github.io/mm1-black/)**
   (**Chrome** or **Edge**).
2. **Wi-Fi** (preferred): Join an AP on the tape, or tap **AP**. Enter the
   address from SETUP → WiFi (or `192.168.4.1` on the MM1 access point),
   then **Install**.
3. **USB UART**: connect a USB UART cable, pick the board, then **Install**.
4. Optional USB **Read** sends `VERSION` → `MM1_FW_VERSION=…`

Images on [GitHub Releases](https://github.com/verlab/mm1-black/releases):

- `MM1-BLACK-denky32-vX.Y.Z.bin`
- `MM1-BLACK-mm1_p4-vX.Y.Z.bin`

Pages also publishes `latest.json` so the device and the installer share the
same current file.

## Lab flash over Join (no USB)

Wi-Fi stays **Off at boot**. After you tap **Join**, HTTP `/update` is on the
STA address shown in SETUP → WiFi (`OTA http://x.x.x.x/update`). Credentials
stay in NVS so Join is one tap; the radio does not start by itself.

```bash
pio run -e mm1_p4
./scripts/ota_lan_flash.py --watch
```

The script scans the LAN (and `192.168.4.1`) for `/api/status` and POSTs
`firmware.bin`. USB loop while opening housings:

```bash
./scripts/usb_flash_p4.sh
```

## On-device update (Join Wi-Fi)

1. SETUP → **WiFi** → enter SSID / password (or **Scan**), then **Join**.
2. After the device has internet, **Check** reads
   `https://verlab.github.io/mm1-black/latest.json` (GitHub API fallback).
3. If a newer tag exists, **Install** streams that board’s `.bin` into the
   inactive OTA slot and reboots.
4. Credentials stay in NVS. Radio stays off at boot until you tap Join.

ESP32-P4 uses the on-board ESP32-C6 (ESP-Hosted). Do not start Wi-Fi if the C6
is offline (the UI will say so). Partition table is dual-slot
(`partitions_mm1_p4.csv`).

## SoftAP (optional)

SETUP → WiFi → **AP** still opens `MM1-MIRA` / `mira-mm1` for CSV export.
`http://192.168.4.1/update` accepts a local `firmware.bin`. Prefer the
GitHub Pages installer (Wi-Fi) or Join + Install on the tape.

## Developers

```bash
pio run -t upload -e denky32
pio run -e mm1_p4 -t upload --upload-port /dev/ttyACM0
```

See [docs/CI.md](docs/CI.md) and [docs/flasher/DEPLOY.md](flasher/DEPLOY.md).
