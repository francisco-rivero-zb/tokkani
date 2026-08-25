# Tokkani

> A small ESP32-C3 desk display for your Claude and Codex usage limits.

Tokkani pairs a Waveshare ST7789V display with a Chrome extension. The extension reads the usage values visible in the official Claude and Codex pages, then sends only normalized percentages to your ESP32 over your local network.

## Highlights

- ESP32-C3 firmware built with PlatformIO and Arduino
- Chrome Manifest V3 extension; no npm build step
- Visible usage capture for Claude and Codex
- Local-device synchronization secured with a user-defined device key
- Captive portal for first-time Wi-Fi setup
- No account-session management, cookies, tokens, passwords, analytics, or remote service

## Repository layout

```text
chrome-extension/  Chrome MV3 extension for usage capture and device sync
firmware/          ESP32-C3 PlatformIO project
```

## Hardware

Tokkani is configured for an **ESP32 Super Mini C3** and a **Waveshare 2 inch ST7789V 240x320 SPI display**.

| Display pin | ESP32-C3 Super Mini pin |
| --- | --- |
| VCC | 3V3 |
| GND | GND |
| DIN / SDA | GPIO6 |
| CLK / SCL | GPIO4 |
| CS | GPIO7 |
| DC / A0 | GPIO10 |
| RST / RES | GPIO3 |
| BL / BLK | GPIO2 |

> [!WARNING]
> Power the display from **3.3 V only**. Verify your board's pin labels before wiring it. Incorrect wiring can damage the display or the ESP32.

The pin mapping lives in [`firmware/include/config.h`](firmware/include/config.h). Change it before building if your wiring is different.

## Requirements

### Required on every operating system

- An ESP32-C3 board and a USB data cable
- The display and wiring listed above
- [Google Chrome](https://www.google.com/chrome/) for the extension
- A Claude and/or Codex account with access to its usage page
- A Wi-Fi network that both your computer and Tokkani can use

The firmware libraries are declared in `firmware/platformio.ini` and are downloaded automatically by PlatformIO on the first build. You do **not** need Arduino IDE, Node.js, npm, or a separate library installation.

### Development environment by operating system

| Operating system | Recommended setup | Notes |
| --- | --- | --- |
| Windows 10/11 | [VS Code](https://code.visualstudio.com/) + [PlatformIO IDE](https://platformio.org/install/ide?install=vscode) | Usually the easiest option. Install the USB driver for your board if Windows does not create a COM port. Common USB chips are CP210x and CH340. |
| macOS | VS Code + PlatformIO IDE | Run `xcode-select --install` if macOS asks for Command Line Tools. For a Python-based CLI installation, run the **Install Certificates.command** supplied by Python if package downloads fail with SSL errors. |
| Linux | VS Code + PlatformIO IDE, or PlatformIO Core CLI | On Debian/Ubuntu, install `python3-venv`. Your user may need serial-port access; see the Linux note below. |

PlatformIO IDE already contains PlatformIO Core. The standalone CLI is optional and is useful if you prefer the terminal. See the official [PlatformIO installation guide](https://docs.platformio.org/en/latest/core/installation/index.html).

## Install PlatformIO

### Option A - VS Code + PlatformIO IDE (recommended)

This method is the same on Windows, macOS, and Linux:

1. Install [Visual Studio Code](https://code.visualstudio.com/).
2. Open VS Code and install the **PlatformIO IDE** extension.
3. Open the cloned Tokkani folder in VS Code.
4. Open the `firmware/` folder, then use PlatformIO's **Build**, **Upload**, and **Serial Monitor** buttons in the status bar.

### Option B - PlatformIO Core CLI

Use the commands for your operating system. The official installer does not require administrator privileges.

**Windows**

1. Install [Python 3](https://www.python.org/downloads/) and enable **Add Python to PATH** in the installer.
2. Download `get-platformio.py` from the [official PlatformIO installer page](https://docs.platformio.org/en/latest/core/installation/methods/installer-script.html).
3. In PowerShell, from the folder containing that file, run:

```powershell
python.exe get-platformio.py
```

Use the PlatformIO shell-command setup described in the installer documentation if `pio` is not available in a new terminal.

**macOS**

```bash
xcode-select --install
curl -fsSL -o get-platformio.py https://raw.githubusercontent.com/platformio/platformio-core-installer/master/get-platformio.py
python3 get-platformio.py
```

**Linux (Debian/Ubuntu example)**

```bash
sudo apt update
sudo apt install python3 python3-venv curl
curl -fsSL -o get-platformio.py https://raw.githubusercontent.com/platformio/platformio-core-installer/master/get-platformio.py
python3 get-platformio.py
```

If uploads fail with a permission error, grant your user serial-port access and sign out/in before trying again:

```bash
sudo usermod -a -G dialout $USER
```

Other Linux distributions use a different package manager and may use `uucp` or another serial-device group. Consult your distribution documentation if `dialout` does not exist.

## Build and flash the ESP32

### 1. Check the pin configuration

Open [`firmware/include/config.h`](firmware/include/config.h) and confirm that the display pins match your wiring. The defaults match the table above.

### 2. Connect the ESP32

Connect the ESP32-C3 with a **USB data cable**. A charging-only cable cannot upload firmware.

Confirm that PlatformIO can see the port:

```bash
cd firmware
pio device list
```

Typical port names:

| OS | Typical port |
| --- | --- |
| Windows | `COM3`, `COM4`, and similar |
| macOS | `/dev/cu.usbmodem*` or `/dev/cu.SLAB_USBtoUART` |
| Linux | `/dev/ttyUSB0` or `/dev/ttyACM0` |

### 3. Build

From the repository root:

```bash
cd firmware
pio run
```

The first build downloads the ESP32 platform, toolchain, and declared libraries. Later builds are much faster.

### 4. Upload

PlatformIO normally detects the port automatically:

```bash
pio run -t upload
```

If automatic detection does not work, specify it explicitly:

```bash
# Windows example
pio run -t upload --upload-port COM3

# macOS example
pio run -t upload --upload-port /dev/cu.usbmodem1101

# Linux example
pio run -t upload --upload-port /dev/ttyUSB0
```

Some ESP32-C3 boards require manual bootloader mode. If the upload cannot connect, hold **BOOT**, briefly press **RESET**, release **RESET**, then release **BOOT** and run the upload again.

### 5. Read the serial log (optional)

```bash
pio device monitor -b 115200
```

Use `Ctrl+C` to close the monitor. The serial log is helpful for checking Wi-Fi setup or diagnosing an upload problem.

### 6. First-time Wi-Fi setup

After the firmware starts, the display shows a temporary Wi-Fi access point named `Tokkani-xxxxxx` and its password.

1. Connect your phone or computer to that access point.
2. Open `http://192.168.4.1`.
3. Enter your Wi-Fi name, Wi-Fi password, and a new device key.
4. Save the form. Tokkani restarts and shows its local IP address.

Keep the device key private. You will enter the same key in the Chrome extension.

## Install the Chrome extension

Tokkani is currently loaded as an unpacked Chrome extension.

1. Open Chrome and visit `chrome://extensions`.
2. Turn on **Developer mode** in the top-right corner.
3. Click **Load unpacked**.
4. Select the repository's `chrome-extension` folder. Select that folder itself, not the repository root.
5. Pin **Tokkani Usage Sync** from Chrome's extensions menu if you want quick access.
6. Click the Tokkani icon and enter:
   - the ESP32 IP address shown after Wi-Fi setup, without `http://`; and
   - the same device key configured on the ESP32.
7. Keep **Sync while usage pages are open** enabled, then click **Save**.

Chrome's unpacked-extension workflow is documented in Google's [extension developer-mode guide](https://support.google.com/chrome/a/answer/2714278).

## Use Tokkani

1. Open the extension popup.
2. Click **Open Claude usage** or **Open Codex analytics**.
3. Sign in to the respective service if needed.
4. Leave the usage page open. Tokkani reads the displayed limits and syncs them to the ESP32 automatically.
5. Use **Sync** in the popup to force a refresh from already open usage pages.

To check that the device is reachable, open `http://<device-ip>/status` in a browser on the same Wi-Fi network.

## Troubleshooting

| Problem | What to check |
| --- | --- |
| No serial port appears | Use a data-capable USB cable. Install the board's USB driver on Windows. On Linux, check your serial-port group. |
| Upload stalls at "Connecting..." | Use the BOOT/RESET sequence above, try another USB cable or port, and specify `--upload-port`. |
| Build fails during first run | Confirm that PlatformIO can reach the internet; it must download the ESP32 toolchain and libraries once. |
| Display is blank | Check 3.3 V power, GND, the backlight pin, and the SPI pin mapping in `config.h`. |
| The extension cannot reach Tokkani | Ensure the computer and ESP32 are on the same network, verify the IP address, and check that the device key matches. |
| No usage appears | Open a supported usage page, wait a few seconds, and use the **Sync** button. The page layout may change over time. |

## Privacy and security

Tokkani reads only visible usage percentages and reset text from the supported usage pages. It sends those values only to the ESP32 address that you configure.

It does not read, store, transmit, inspect, monitor, or modify account sessions. It does not access cookies, authentication tokens, passwords, local-storage credentials, raw API responses, analytics, or third-party services. The extension stores its device settings only in Chrome's local extension storage.

## Useful PlatformIO commands

```bash
# Build the firmware
pio run

# Upload it
pio run -t upload

# Remove local build output
pio run -t clean

# List serial devices
pio device list

# Open the serial monitor
pio device monitor -b 115200
```

For PlatformIO command details, see the official [`pio run` documentation](https://docs.platformio.org/en/latest/core/userguide/cmd_run.html).
