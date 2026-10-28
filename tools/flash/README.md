# Building and flashing the Silvia

`flash-silvia.sh` builds the firmware in Docker and uploads it over the air (OTA). It is safe
to re-run: it checks the repository, Docker and the machine before it asks for anything, and
nothing is flashed until you type `flash`.

```bash
tools/flash/flash-silvia.sh            # build + flash
tools/flash/flash-silvia.sh --build    # build only, placeholder secrets, no machine needed
```

## Local settings

The machine address and the WiFi name are not secret, but they don't belong in a public
repository. Put them into the untracked `tools/flash/local.env` (or the environment):

```bash
SILVIA_HOST=192.0.2.10   # IP or hostname of the machine, default silvia.local
SILVIA_SSID=my-wifi      # offered as default at the WiFi prompt
```

## What the script does

1. **Check**: template present, tag `v3.3.0-esp8266` present, Docker running, machine
   reachable and running the expected firmware. All machine parameters are backed up to
   `tools/flash/backups/` (untracked).
2. **Credentials**: WiFi password, current and new OTA password are pasted (hidden input).
   Keep them in 1Password; they only live in the shell process and, during the build, in
   `src/userConfig.h` (mode 600, untracked).
3. **Build** in `python:3.12-slim` with PlatformIO 6.1.18. The xtensa toolchain of
   espressif8266 2.6.x only exists as an x86 binary, and the Mac has no Rosetta. Afterwards
   `src/userConfig.h` is rewritten with placeholders.
4. **Flash** with `espota.py` directly from the Mac (the ESP8266 cannot connect back into
   the container). macOS may ask whether python may accept incoming connections → Allow.
   The ESP8266 only switches to the new firmware once the upload is complete; if the upload
   breaks off, the old firmware keeps running.
5. **Verify** after the reboot: the machine is back, PID is on and the settings are
   unchanged. A protocol is printed at the end.

## Config

`userConfig.silvia.h` is the build configuration. The `@...@` placeholders are filled in by
the script. Notable settings compared to the upstream sample config:

| Setting | Upstream sample | Silvia | Why |
|---|---|---|---|
| `BREWDETECTION` | 0 | **1** | software brew detection from the temperature drop (the machine has no brew switch) |
| `BREWPID_DELAY` | 10 | **0** | 3.3.0 freezes the PID output during the delay instead of stopping the heater |
| `OTAPASS` | `otapass` | prompted | the default lets anyone on the WiFi flash the heater controller |

## Recovery over USB

If the machine doesn't come back into the WiFi after flashing (e.g. a mistyped WiFi
password), it keeps heating with its stored settings. Reflash over USB on the NodeMCU:

1. Fill `src/userConfig.h` from `userConfig.silvia.h` with the correct WiFi and OTA values
   (the script removes them after every build, so do this by hand).
2. Build the USB image:

   ```bash
   docker run --rm -v "$PWD":/fw -v silvia-pio:/root/.platformio -w /fw python:3.12-slim sh -c "apt-get update -qq && apt-get install -y -qq git && pip install -q platformio==6.1.18 && pio run -e nodemcuv2_usb"
   ```

3. Flash it:

   ```bash
   pip install esptool && esptool.py --port /dev/cu.usbserial-* write_flash 0x0 .pio/build/nodemcuv2_usb/firmware.bin
   ```

4. Put the placeholders back: `tools/flash/flash-silvia.sh --build`.

## Tests

See the top-level README. The recorded shots used by the tests are in
`tools/silvia-sim/data/` (time, temperature, heater power at 1 Hz).
`tools/silvia-sim/silvia_sim.py` is an experimental thermal model; it only matches the
measurements to about 1 °C and is not used to derive parameters.
