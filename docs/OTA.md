# Firmware update

## On device (SETUP → About / WiFi)

Shows **FW_VERSION** and a QR code to:

**https://verlab.github.io/mm1-black/**

P4 QR opens `?board=p4`. Wi-Fi defaults **Off**. Join a home/lab access point
to check GitHub Pages for a newer release and install it on the device.

## USB installer (PC)

1. Connect **MM1-BLACK** by USB to a PC (**Chrome** or **Edge**).
2. Open the **[firmware installer](https://verlab.github.io/mm1-black/)**.
3. Pick **CYD** or **P4**, select a release, confirm the checkbox, click **Install**.
4. Optional: **Read** sends `VERSION` (CYD 9600 baud, P4 115200) → `MM1_FW_VERSION=…`

P4 uses the **USB TO UART** port (QinHeng `1a86:55d3`), not USB OTG. A blank
board still needs a first PlatformIO USB flash (bootloader + partitions + app).
This page writes the **app** image (`firmware.bin` @ `0x10000`).

Images on [GitHub Releases](https://github.com/verlab/mm1-black/releases):

- `MM1-BLACK-denky32-vX.Y.Z.bin`
- `MM1-BLACK-mm1_p4-vX.Y.Z.bin`

Pages also publishes `latest.json` so the device and the installer share the
same current file.

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
`http://192.168.4.1/update` is last-resort local `firmware.bin` only — prefer
the GitHub Pages installer or Join + Install.

## Developers

```bash
pio run -t upload -e denky32
pio run -e mm1_p4 -t upload --upload-port /dev/ttyACM0
```

See [docs/CI.md](docs/CI.md) and [docs/flasher/DEPLOY.md](flasher/DEPLOY.md).
