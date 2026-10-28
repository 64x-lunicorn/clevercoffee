# Clever Coffee – Silvia-only fork

This is a **hard fork** of [Clever Coffee](https://github.com/rancilio-pid/clevercoffee)
(formerly Rancilio PID), based on the last ESP8266 release `v3.3.0-esp8266`. It supports
exactly one machine:

| | |
|---|---|
| Machine | Rancilio Silvia E |
| Board | NodeMCU (ESP8266) on the Clever Coffee PCB |
| Sensor | TSIC306 |
| Display | SSD1306 128×64 (I2C) |
| Control | PID only (no pump/valve relays, no brew or steam switch), software brew detection |

If you are looking for a general-purpose PID controller for your espresso machine, use the
upstream project: <https://github.com/rancilio-pid/clevercoffee> and
<https://clevercoffee.de>. Nothing from this fork is intended to be merged back.

## Credits and licence

All credit for the original firmware, hardware and documentation goes to the Clever Coffee /
Rancilio PID authors and contributors. This fork is distributed under the same licence,
the GNU General Public License v2 (see [LICENSE](LICENSE)). Files changed in this fork carry
a modification notice.

## Status

The fork is delivered in steps, each flashed and checked with a test coffee:

1. **Cleanup** – code for other machines, the ESP32, scales, ToF water level, pressure
   sensors, brew/steam/power switches, backflush, InfluxDB and the E-trigger removed.
   Behaviour unchanged.
2. **Core** – all decisions in a hardware-free, unit-tested core (`lib/silvia_core`): sensor
   gate, emergency stop in every state, machine states, PID tunings, shot detection with
   back-dated start and end, one parameter table. WiFi reconnects forever in the background,
   the heater decision is applied before the display refresh, new display with shot timer and
   plain-language errors, JSON API ([docs/api.md](docs/api.md)) with basic auth for writes.
3. On-device history and shot log, JSON API, offline web UI.
4. Home Assistant via MQTT discovery.

The full specification is [issue #1](https://github.com/64x-lunicorn/clevercoffee/issues/1).

## Build, test, flash

- Native unit tests (on the host, no machine needed):

  ```bash
  ~/.platformio/penv/bin/pio test -e native
  ```

- Firmware build only (Docker; the espressif8266 2.6.x xtensa toolchain is x86-only):

  ```bash
  tools/flash/flash-silvia.sh --build
  ```

- Build and flash over the air: `tools/flash/flash-silvia.sh` – see
  [tools/flash/README.md](tools/flash/README.md). Credentials are prompted at flash time and
  never stored in the repository.

## Repository layout

| Path | Content |
|---|---|
| `lib/silvia_core/` | hardware-free core: everything that decides something |
| `src/` | thin hardware adapters: sensor, heater ISR, EEPROM, WiFi/OTA, web API, display |
| `docs/api.md` | HTTP API |
| `test/` | native Unity tests (`pio test -e native`) |
| `tools/flash/` | config template, flash script, vendored `espota.py` |
| `tools/silvia-sim/` | recorded shots (`data/*.jsonl`: time, temperature, heater power) and the test-vector generator |
| `data/` | web UI files for LittleFS |
| `hardware/`, `img/` | upstream PCB and wiring documentation |
