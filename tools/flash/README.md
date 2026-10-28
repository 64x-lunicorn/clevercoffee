# Silvia: Clever Coffee 3.3.0 mit Software-Brüherkennung

Die Silvia lief mit `v3.3.0-esp8266`, gebaut aus der unveränderten
`userConfig_sample.h` – darin steht `BREWDETECTION 0`. Dadurch sind alle Brew-PID-Felder
(`PID_BD_*`) wirkungslos, die Weboberfläche speichert sie nur.

Dieser Branch (`silvia-bd`) ändert nur die Konfiguration, nicht den Firmware-Code:

| Einstellung | Vorlage | Silvia | Warum |
|---|---|---|---|
| `BREWDETECTION` | 0 | **1** | Software-Brüherkennung über den Temperaturabfall |
| `BREWPID_DELAY` | 10 | **0** | Laut Kommentar „kein Heizen“ während der Wartezeit – tatsächlich friert 3.3.0 nur den PID-Ausgang ein (`SetMode(MANUAL)`), die Heizung bleibt auf dem letzten Wert |
| `OTAPASS` | `otapass` | neu | Standardpasswort = jeder im WLAN kann die Heizungssteuerung flashen |
| WLAN | Platzhalter | per Skript | Passwörter kommen nie ins Repo |

## Flashen

```bash
tools/flash/flash-silvia.sh
```

Das Skript prüft erst Repo, Docker und Maschine, sichert die Parameter nach
`tools/flash/backups/`, fragt dann WLAN- und OTA-Passwort ab (Cmd+V), baut in Docker und
flasht erst nach Eingabe von `flash`. Danach prüft es, ob die Brew-PID-Felder sichtbar sind
und alle Einstellungen erhalten blieben, und zeigt ein Protokoll.

Nur bauen, ohne Passwörter und ohne Maschine: `tools/flash/flash-silvia.sh --build`

Gebaut wird in Docker (`python:3.12-slim`, PlatformIO 6.1.18), weil die Toolchain von
espressif8266 2.6.x nur als Intel-Binary existiert und dieser Mac kein Rosetta hat.
Hochgeladen wird mit `espota.py` direkt vom Mac (aus dem Container heraus kann der ESP8266
nicht zurückverbinden).

## Nach dem Flashen: erster Test-Shot

Über die Weboberfläche (Parameters → Brew Detection) für den ersten Shot:

- Enable Brew PID: 1, BD Kp 120, BD Tn 0, BD Tv 20 – sind schon gesetzt
- **PID BD Time: 30 s** statt 40 s. Die Erkennung löst ~4 s nach Shot-Beginn aus, bei
  ~35 s Shots endet der Brew-PID damit ungefähr mit dem Shot. Länger heizen = mehr
  Überschwingen danach.
- PID BD Sensitivity: 120 lassen

Dann einen Shot mitloggen und mit `data/shot1.jsonl` / `data/shot2.jsonl` vergleichen.

## Tests

```bash
~/.platformio/penv/bin/pio test -e native
```

Native Unity-Tests (laufen auf dem Mac, ohne Maschine):

- `test/test_pid` – Verhalten der PID-Library mit den Firmware-Einstellungen
  (Anti-Windup, I-Max, Tn = 0 setzt den Integrator zurück, MANUAL friert den Ausgang ein)
- `test/test_brew_detection` – `TempRateFilter` + `isSoftwareBrewStart` auf Rampen und auf
  den echten Aufzeichnungen (`test/test_brew_detection/recorded_shots.h`, erzeugt mit
  `tools/silvia-sim/gen_test_vectors.py`)
- `test/test_web_safety` – Prüfung der POST-Werte (`ParameterInput.h`) und `HeaterIsrPause`

Sicherheitsfixes gegenüber 3.3.0 in diesem Branch:

- POST `/parameters` schreibt `TEMP`/`VERSION` nicht mehr zurück (die Weboberfläche schickt
  beim Speichern die beim Laden angezeigte Temperatur mit, die landete im Regler)
- POST `/parameters` prüft min/max und lehnt Unsinn ab statt ihn als 0 zu übernehmen (HTTP 400)
- `skipHeaterISR` wird über `HeaterIsrPause` auf jedem Rückweg zurückgesetzt – vorher blieb es
  nach jeder ausgelieferten HTML-Seite `true` und der Heizungsausgang hing fest
- BD Tn: Speicherlimit 999 statt versehentlich 200 (`PID_KP_BD_MAX`)

`tools/silvia-sim/test_brew_detection.py` spielt die Erkennungslogik der Firmware
(`calculateTemperatureMovingAverage()` + `brewDetection()`) über die echten Aufzeichnungen:
bei Empfindlichkeit 120 alle 3 Shots nach 3,7–4,4 s erkannt, keine Fehlauslösung in ~30 min
Leerlauf und bei einer 4-minütigen Heißwasser-Entnahme.

`tools/silvia-sim/silvia_sim.py` (Thermomodell + PID-Nachbau) ist ein Versuch – das Modell
trifft die Messungen nur auf ~1 °C genau und taugt noch nicht, um PID-Werte daraus abzuleiten.

## Notfall: Maschine kommt nach dem Flashen nicht ins WLAN

Die Silvia heizt auch ohne WLAN weiter mit den gespeicherten Werten. Neu flashen geht dann
nur per USB am NodeMCU:

```bash
docker run --rm -v "$PWD":/fw -v silvia-pio:/root/.platformio -w /fw python:3.12-slim sh -c "apt-get update -qq && apt-get install -y -qq git && pip install -q platformio==6.1.18 && pio run -e nodemcuv2_usb"
```

```bash
pip install esptool && esptool.py --port /dev/cu.usbserial-* write_flash 0x0 .pio/build/nodemcuv2_usb/firmware.bin
```

(vorher `src/userConfig.h` mit den richtigen WLAN-Daten füllen – das Skript entfernt sie
nach jedem Build wieder.)
