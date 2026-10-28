#!/usr/bin/env bash
# Builds the Silvia-only Clever Coffee fork (ESP8266) and flashes it to the Silvia over OTA. Safe to re-run: it checks everything before it asks anything,
# and nothing is flashed until you type "flash".
#
#   tools/flash/flash-silvia.sh            # build + flash
#   tools/flash/flash-silvia.sh --build    # build only (placeholder secrets, no flashing)
set -euo pipefail

REPO="$(cd "$(dirname "$0")/../.." && pwd)"
# Machine address and WiFi name are not secret, but they don't belong in a public repo.
# Set them in the environment or in the untracked tools/flash/local.env:
#   SILVIA_HOST=192.0.2.10
#   SILVIA_SSID=my-wifi
[[ -f "$REPO/tools/flash/local.env" ]] && source "$REPO/tools/flash/local.env"
DEVICE_IP="${SILVIA_HOST:-silvia.local}"
DEFAULT_SSID="${SILVIA_SSID:-}"
# firmware version that will be built, e.g. "3.3.0 SILVIA-1"
NEW_VERSION="3.3.0 $(sed -nE 's/^#define FW_BRANCH +"([^"]+)".*/\1/p' "$REPO/src/rancilio-pid.cpp")"
TEMPLATE="$REPO/tools/flash/userConfig.silvia.h"
CONFIG="$REPO/src/userConfig.h"
BIN="$REPO/.pio/build/nodemcuv2_ota/firmware.bin"
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

write_config() {  # write_config SSID WIFI_PASS OTA_PASS
    python3 -I - "$TEMPLATE" "$CONFIG" "$1" "$2" "$3" <<'EOF'
import sys
template, target, ssid, wifi, ota = sys.argv[1:]
def c(s):  # C string literal body
    return s.replace("\\", "\\\\").replace('"', '\\"')
src = open(template).read()
src = src.replace("@WIFI_SSID@", c(ssid)).replace("@WIFI_PASS@", c(wifi)).replace("@OTA_PASS@", c(ota))
open(target, "w").write(src)
EOF
    chmod 600 "$CONFIG"
}

scrub_config() {  # never leave real secrets in the working tree
    write_config "@WIFI_SSID@" "@WIFI_PASS@" "@OTA_PASS@"
}

build() {
    docker run --rm -v "$REPO":/fw -v silvia-pio:/root/.platformio -w /fw python:3.12-slim sh -c \
        "apt-get update -qq >/dev/null && apt-get install -y -qq git >/dev/null \
         && pip install -q --root-user-action=ignore platformio==6.1.18 \
         && pio run -e nodemcuv2_ota" >"$REPO/.pio-build.log" 2>&1
}

device_params() {
    curl -s -m 5 "http://$DEVICE_IP/parameters"
}

param() {  # param JSON NAME [field]
    python3 -I -c 'import json,sys; d={p["name"]:p for p in json.loads(sys.argv[1])}; print(d[sys.argv[2]][sys.argv[3]])' "$1" "$2" "${3:-value}"
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
    rm -f "$BIN" "${BIN%.bin}.elf"
    protocol
    exit 0
fi

PARAMS_BEFORE="$(device_params)" || true
[[ -n "$PARAMS_BEFORE" ]] || die "Silvia unter http://$DEVICE_IP nicht erreichbar – Maschine an? Gleiches WLAN?"
VERSION="$(param "$PARAMS_BEFORE" VERSION)"
[[ "$VERSION" == "3.3.0 "* ]] || die "Unerwartete Firmware auf der Maschine: '$VERSION' (erwartet 3.3.0 ...)"
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

# ------------------------------------------------------------------ build
say "3/5 Bauen (Docker, ca. 1 Minute)"
write_config "$SSID" "$WIFI_PASS" "$NEW_OTA"
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
  Neu:       $(git describe --tags --always --dirty) ($(git branch --show-current))
  Bleibt:    alle Einstellungen (gleicher Speicheraufbau)
  Risiko:    gering – der ESP8266 übernimmt das Update erst, wenn es komplett angekommen ist.
             Bricht es ab, läuft die alte Firmware weiter.
  Achtung:   Nicht während eines Shots. macOS fragt evtl., ob python eingehende
             Verbindungen annehmen darf -> "Erlauben".
EOF
read -r -p "  Zum Flashen 'flash' tippen: " CONFIRM
if [[ "$CONFIRM" != "flash" ]]; then
    rm -f "$BIN" "${BIN%.bin}.elf"
    warn "Abgebrochen, nichts geflasht"
    protocol
    exit 0
fi
if python3 -I "$REPO/tools/flash/espota.py" -i "$DEVICE_IP" -p 8266 -a "$OLD_OTA" -f "$BIN" -r; then
    ok "Upload abgeschlossen"
else
    rm -f "$BIN" "${BIN%.bin}.elf"
    die "Upload fehlgeschlagen – alte Firmware läuft weiter. Häufigste Ursache: falsches aktuelles OTA-Passwort oder macOS-Firewall"
fi
rm -f "$BIN" "${BIN%.bin}.elf"

# ------------------------------------------------------------------ verify
say "5/5 Prüfen nach dem Neustart"
PARAMS_AFTER=""
for _ in $(seq 1 45); do
    sleep 2
    PARAMS_AFTER="$(device_params)" || true
    [[ -n "$PARAMS_AFTER" ]] && break
done
[[ -n "$PARAMS_AFTER" ]] || die "Silvia nach 90 s nicht wieder im WLAN – WLAN-Daten falsch? Dann per USB neu flashen (siehe tools/flash/README.md)"
VERSION_AFTER="$(param "$PARAMS_AFTER" VERSION)"
[[ "$VERSION_AFTER" == "$NEW_VERSION" ]] \
    && ok "Silvia wieder erreichbar, läuft $VERSION_AFTER" \
    || warn "Silvia wieder erreichbar, läuft aber '$VERSION_AFTER' statt '$NEW_VERSION'"
[[ "$(param "$PARAMS_AFTER" PID_BD_ON show)" == "True" ]] \
    && ok "Brüherkennung jetzt aktiv (Brew-PID-Felder werden angezeigt)" \
    || warn "Brew-PID-Felder werden noch nicht angezeigt – Brüherkennung nicht aktiv?"
[[ "$(param "$PARAMS_AFTER" PID_ON)" == "1" ]] && ok "PID läuft" || warn "PID ist AUS – in der Weboberfläche einschalten"
for p in BREW_SET_POINT PID_KP PID_TN PID_TV PID_BD_ON PID_BD_KP PID_BD_TIME PID_BD_BREWSENSITIVITY; do
    b="$(param "$PARAMS_BEFORE" $p)"; a="$(param "$PARAMS_AFTER" $p)"
    [[ "$a" == "$b" ]] && ok "$p = $a (unverändert)" || warn "$p: vorher $b, jetzt $a"
done
[[ "$NEW_OTA" != "$OLD_OTA" ]] && warn "Ab jetzt gilt das neue OTA-Passwort aus 1Password"
protocol
