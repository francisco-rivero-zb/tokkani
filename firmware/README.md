# Tokkani firmware

This directory is the ESP32-C3 PlatformIO project for Tokkani.

For the complete prerequisites, wiring table, cross-platform setup instructions, build/upload commands, Wi-Fi setup, and troubleshooting, see the [root README](../README.md).

## Quick reference

```bash
cd firmware
pio run
pio run -t upload
pio device monitor -b 115200
```

The firmware accepts normalized usage readings at `POST /usage` with the `X-Tokkani-Key` header. Use `GET /status` to verify that the device is reachable.
