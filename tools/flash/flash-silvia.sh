#!/usr/bin/env bash
# Builds the Silvia-only Clever Coffee fork (ESP8266) and flashes it to the Silvia over OTA.
# Safe to re-run: it checks everything before it asks anything, and nothing is flashed until
# you type "flash".
#
#   tools/flash/flash-silvia.sh            # build + flash
#   tools/flash/flash-silvia.sh --build    # build only (placeholder secrets, no flashing)
set -euo pipefail

REPO="$(cd "$(dirname "$0")/../.." && pwd)"
# Machine address and WiFi name are not secret, but they don't belong in a public repo.
# Set them in the environment or in the untracked tools/flash/local.env:
#   SILVIA_HOST=192.0.2.10
#   SILVIA_SSID=my-wifi
#   SILVIA_MQTT_HOST=192.0.2.20   # Home Assistant / Mosquitto
[[ -f "$REPO/tools/flash/local.env" ]] && source "$REPO/tools/flash/local.env"
DEVICE_IP="${SILVIA_HOST:-silvia.local}"
DEFAULT_SSID="${SILVIA_SSID:-}"
DEFAULT_MQTT_HOST="${SILVIA_MQTT_HOST:-}"
# firmware version that will be built, e.g. "silvia-2"
NEW_VERSION="$(sed -nE 's/^#define SILVIA_VERSION +"([^"]+)".*/\1/p' "$REPO/src/Version.h")"
TEMPLATE="$REPO/tools/flash/userConfig.silvia.h"
CONFIG="$REPO/src/userConfig.h"
BIN="$REPO/.pio/build/nodemcuv2_ota/firmware.bin"
FS_BIN="$REPO/.pio/build/nodemcuv2_ota/littlefs.bin"   # web UI (data/www, built by web/build.py)
BACKUPS="$REPO/tools/flash/backups"
LOG=()

say()  { printf '\n\033[1m%s\033[0m\n' "$*"; }
ok()   { printf '  \033[32m✔\033[0m %s\n' "$*"; LOG+=("✔ $*"); }
warn() { printf '  \033[33m!\033[0m %s\n' "$*"; LOG+=("! $*"); }
die()  { printf '  \033[31m✘ %s\033[0m\n' "$*"; LOG+=("✘ $*"); protocol; exit 1; }

protocol() {
    say "Protokoll"
    printf '  %s\n' "${LOG[@]}"
}

write_config() {  # write_config SSID WIFI_PASS OTA_PASS WEB_PASS MQTT_HOST MQTT_PASS
    python3 -I - "$TEMPLATE" "$CONFIG" "$@" <<'EOF'
import sys
template, target, ssid, wifi, ota, web, mqtt_host, mqtt_pass = sys.argv[1:]
def c(s):  # C string literal body
    return s.replace("\\", "\\\\").replace('"', '\\"')
src = open(template).read()
for name, value in (("WIFI_SSID", ssid), ("WIFI_PASS", wifi), ("OTA_PASS", ota), ("WEB_PASS", web),
                    ("MQTT_HOST", mqtt_host), ("MQTT_PASS", mqtt_pass)):
    src = src.replace(f"@{name}@", c(value))
open(target, "w").write(src)
EOF
    chmod 600 "$CONFIG"
}

scrub_config() {  # never leave real secrets in the working tree
    write_config "@WIFI_SSID@" "@WIFI_PASS@" "@OTA_PASS@" "@WEB_PASS@" "" "@MQTT_PASS@"
}

build_web() {
    python3 -I "$REPO/web/build.py" >/dev/null
}

cleanup_bins() {  # the images contain the compiled-in passwords
    rm -f "$BIN" "${BIN%.bin}.elf" "$FS_BIN"
}

build() {
    build_web || return 1
    docker run --rm -v "$REPO":/fw -v silvia-pio:/root/.platformio -w /fw python:3.12-slim sh -c \
        "apt-get update -qq >/dev/null && apt-get install -y -qq git >/dev/null \
         && pip install -q --root-user-action=ignore platformio==6.1.18 \
         && pio run -e nodemcuv2_ota && pio run -e nodemcuv2_ota -t buildfs" >"$REPO/.pio-build.log" 2>&1
}

machine() {  # {"version": ..., "params": {...}} from either firmware generation
    python3 -I "$REPO/tools/flash/machine.py" "$DEVICE_IP"
}

field() {  # field JSON version | field JSON params KEY
    python3 -I -c 'import json,sys; d=json.loads(sys.argv[1]); print(d["version"] if sys.argv[2]=="version" else d["params"][sys.argv[3]])' "$@"
}

# ------------------------------------------------------------------ checks
say "1/5 Prüfen"
cd "$REPO"
[[ -f "$TEMPLATE" ]] || die "Vorlage fehlt: $TEMPLATE"
git rev-parse --verify -q v3.3.0-esp8266 >/dev/null || die "Tag v3.3.0-esp8266 fehlt im Repo"
ok "Firmware-Quelle: $(git describe --tags --always) auf Branch $(git branch --show-current)"
docker info >/dev/null 2>&1 || die "Docker läuft nicht – Docker Desktop starten und Skript neu starten"
ok "Docker läuft"

if [[ "${1:-}" == "--build" ]]; then
    say "Nur Bauen (Platzhalter statt Passwörter)"
    scrub_config
    build || { tail -30 "$REPO/.pio-build.log"; die "Build fehlgeschlagen (.pio-build.log)"; }
    ok "Build ok: $(grep -E '^(RAM|Flash):' "$REPO/.pio-build.log" | tr -s ' ' | paste -sd ' ' -)"
    cleanup_bins
    protocol
    exit 0
fi

PARAMS_BEFORE="$(machine)" || true
[[ -n "$PARAMS_BEFORE" ]] || die "Silvia unter http://$DEVICE_IP nicht erreichbar – Maschine an? Gleiches WLAN?"
VERSION="$(field "$PARAMS_BEFORE" version)"
[[ "$VERSION" == "3.3.0 "* || "$VERSION" == silvia-* ]] || die "Unerwartete Firmware auf der Maschine: '$VERSION'"
ok "Silvia erreichbar, läuft $VERSION"
mkdir -p "$BACKUPS"
STAMP="$(date +%Y%m%d-%H%M%S)"
echo "$PARAMS_BEFORE" > "$BACKUPS/params-$STAMP.json"
ok "Parameter gesichert: tools/flash/backups/params-$STAMP.json"

# ------------------------------------------------------------------ secrets
say "2/5 Zugangsdaten (Einfügen mit Cmd+V, Eingabe bleibt unsichtbar)"
read -r -p "  WLAN-Name${DEFAULT_SSID:+ [$DEFAULT_SSID]}: " SSID
SSID="${SSID:-$DEFAULT_SSID}"
[[ -n "$SSID" ]] || die "Kein WLAN-Name angegeben"
read -r -s -p "  WLAN-Passwort für '$SSID': " WIFI_PASS; echo
[[ ${#WIFI_PASS} -ge 8 ]] || die "WLAN-Passwort kürzer als 8 Zeichen – vermutlich nicht richtig eingefügt"
ok "WLAN '$SSID', Passwort mit ${#WIFI_PASS} Zeichen"
read -r -s -p "  Aktuelles OTA-Passwort der Maschine [otapass]: " OLD_OTA; echo
OLD_OTA="${OLD_OTA:-otapass}"
echo "  Neues OTA-Passwort: vorher in 1Password anlegen (z. B. 'Silvia OTA'), dann hier einfügen."
read -r -s -p "  Neues OTA-Passwort (leer = aktuelles behalten): " NEW_OTA; echo
NEW_OTA="${NEW_OTA:-$OLD_OTA}"
if [[ "$NEW_OTA" == "$OLD_OTA" ]]; then
    warn "OTA-Passwort bleibt unverändert"
else
    ok "Neues OTA-Passwort mit ${#NEW_OTA} Zeichen (gilt ab der neuen Firmware)"
fi
echo "  Web-Passwort (Benutzer 'silvia', für jede Änderung über Web/API): in 1Password z. B. 'Silvia Web'."
read -r -s -p "  Web-Passwort: " WEB_PASS; echo
[[ ${#WEB_PASS} -ge 8 ]] || die "Web-Passwort kürzer als 8 Zeichen – vermutlich nicht richtig eingefügt"
ok "Web-Passwort mit ${#WEB_PASS} Zeichen"
read -r -p "  MQTT-Broker (IP von Home Assistant, leer = MQTT aus)${DEFAULT_MQTT_HOST:+ [$DEFAULT_MQTT_HOST]}: " MQTT_HOST
MQTT_HOST="${MQTT_HOST:-$DEFAULT_MQTT_HOST}"
if [[ -n "$MQTT_HOST" ]]; then
    read -r -s -p "  MQTT-Passwort für Benutzer 'silvia': " MQTT_PASS; echo
    [[ -n "$MQTT_PASS" ]] || die "MQTT-Passwort leer – Benutzer 'silvia' in Mosquitto anlegen (siehe docs/home-assistant.md)"
    ok "MQTT an $MQTT_HOST als 'silvia', Passwort mit ${#MQTT_PASS} Zeichen"
else
    MQTT_PASS=""
    warn "MQTT aus"
fi

# ------------------------------------------------------------------ build
say "3/5 Bauen (Docker, ca. 1 Minute)"
write_config "$SSID" "$WIFI_PASS" "$NEW_OTA" "$WEB_PASS" "$MQTT_HOST" "$MQTT_PASS"
trap scrub_config EXIT
if ! build; then
    tail -30 "$REPO/.pio-build.log"
    die "Build fehlgeschlagen (.pio-build.log)"
fi
scrub_config
ok "Firmware gebaut: $(grep -E '^(RAM|Flash):' "$REPO/.pio-build.log" | tr -s ' ' | paste -sd ' ' -)"
ok "Passwörter aus src/userConfig.h wieder entfernt"

# ------------------------------------------------------------------ flash
say "4/5 Flashen"
cat <<EOF
  Ziel:      Silvia $DEVICE_IP ($VERSION)
  Neu:       $NEW_VERSION, $(git describe --tags --always --dirty) ($(git branch --show-current))
  Bleibt:    alle Einstellungen (gleicher Speicheraufbau)
  Risiko:    gering – der ESP8266 übernimmt das Update erst, wenn es komplett angekommen ist.
             Bricht es ab, läuft die alte Firmware weiter.
  Achtung:   Nicht während eines Shots. macOS fragt evtl., ob python eingehende
             Verbindungen annehmen darf -> "Erlauben".
EOF
read -r -p "  Zum Flashen 'flash' tippen: " CONFIRM
if [[ "$CONFIRM" != "flash" ]]; then
    cleanup_bins
    warn "Abgebrochen, nichts geflasht"
    protocol
    exit 0
fi
if python3 -I "$REPO/tools/flash/espota.py" -i "$DEVICE_IP" -p 8266 -a "$OLD_OTA" -f "$BIN" -r; then
    ok "Firmware hochgeladen"
else
    cleanup_bins
    die "Upload fehlgeschlagen – alte Firmware läuft weiter. Häufigste Ursache: falsches aktuelles OTA-Passwort oder macOS-Firewall"
fi

# the web UI goes into LittleFS; the new firmware unmounts it during the update
for _ in $(seq 1 45); do
    sleep 2
    machine >/dev/null 2>&1 && break
done
if python3 -I "$REPO/tools/flash/espota.py" -i "$DEVICE_IP" -p 8266 -a "$NEW_OTA" -s -f "$FS_BIN" -r; then
    ok "Web-Oberfläche hochgeladen"
else
    warn "Web-Oberfläche nicht hochgeladen – die Maschine zeigt die einfache Statusseite. Skript erneut starten."
fi
cleanup_bins

# ------------------------------------------------------------------ verify
say "5/5 Prüfen nach dem Neustart"
PARAMS_AFTER=""
for _ in $(seq 1 45); do
    sleep 2
    PARAMS_AFTER="$(machine)" || true
    [[ -n "$PARAMS_AFTER" ]] && break
done
[[ -n "$PARAMS_AFTER" ]] || die "Silvia nach 90 s nicht wieder im WLAN – WLAN-Daten falsch? Dann per USB neu flashen (siehe tools/flash/README.md)"
echo "$PARAMS_AFTER" > "$BACKUPS/params-$STAMP-after.json"
VERSION_AFTER="$(field "$PARAMS_AFTER" version)"
[[ "$VERSION_AFTER" == "$NEW_VERSION" ]] \
    && ok "Silvia wieder erreichbar, läuft $VERSION_AFTER" \
    || warn "Silvia wieder erreichbar, läuft aber '$VERSION_AFTER' statt '$NEW_VERSION'"
if python3 -I "$REPO/tools/flash/machine.py" diff "$BACKUPS/params-$STAMP.json" "$BACKUPS/params-$STAMP-after.json" | sed 's/^/    /'; then
    ok "Alle Einstellungen unverändert"
else
    warn "Einstellungen geändert (Zeilen mit '!' oben)"
fi
[[ "$(field "$PARAMS_AFTER" params pid_on)" =~ ^(1|True|true)$ ]] && ok "Regelung an" || warn "Regelung ist AUS"
[[ "$NEW_OTA" != "$OLD_OTA" ]] && warn "Ab jetzt gilt das neue OTA-Passwort aus 1Password"
protocol
