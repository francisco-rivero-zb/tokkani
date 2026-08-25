# Tokkani Chrome extension

This directory contains the Chrome Manifest V3 extension that reads visible Claude and Codex usage values and synchronizes them with Tokkani.

For the complete installation steps, first-time configuration, supported pages, and privacy details, see the [root README](../README.md).

## Quick install

1. Open `chrome://extensions`.
2. Enable **Developer mode**.
3. Click **Load unpacked** and select this `chrome-extension` directory.
4. Open the Tokkani popup and enter the ESP32 IP address plus the same device key configured on the device.

The extension only sends normalized readings to the ESP32 address you configure. It does not access account sessions, cookies, or authentication tokens.
