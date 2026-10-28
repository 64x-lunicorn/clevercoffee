# Home Assistant

The Silvia appears in Home Assistant through **MQTT discovery** on the Mosquitto broker app
(add-on) of Home Assistant. There is no YAML to write: the device "Silvia" and its entities are
created automatically once the machine connects.

## 1. MQTT user for the machine

The machine logs in as its own MQTT user `silvia`, so its password is not shared with anything
else. Create it in the Mosquitto app (it is an MQTT-only login, not a Home Assistant user):

1. Create a password in 1Password, e.g. item "Silvia MQTT", username `silvia`.
2. Home Assistant → **Settings → Apps** (Add-ons) → **Mosquitto broker** → tab **Configuration**.
3. Under **Logins** add an entry (the editor shows YAML for the list):

   ```yaml
   - username: silvia
     password: <paste from 1Password>
   ```

4. **Save**, then on the **Info** tab **Restart** the app. Zigbee2MQTT and the other MQTT
   clients reconnect by themselves within seconds.

Then flash the firmware with `tools/flash/flash-silvia.sh`: it asks for the broker address
(the Home Assistant IP; `SILVIA_MQTT_HOST` in `tools/flash/local.env` is offered as default) and
the MQTT password. An empty broker address builds the firmware without MQTT.

## 2. Topics

| Topic | Content |
|---|---|
| `silvia/status` | `online` / `offline` (retained; `offline` is the last will, so HA shows the entities as unavailable when the machine is off) |
| `silvia/state` | measurements and state, JSON, every second while heating, during a shot, recovery or steam, every 10 s when idle, at once on a state change |
| `silvia/settings` | set points, switches and PID parameters, JSON, retained, on every change |
| `silvia/event/shot` | one message per finished shot: `{"event_type":"shot","id","duration_s","start_temp","min_temp","start_unix"}` |
| `silvia/set/<key>` | commands: `brew_setpoint`, `steam_setpoint`, `standby_min` (numbers), `steam_on`, `pid_on` (`ON`/`OFF`) |
| `homeassistant/status` | the machine re-publishes discovery when HA announces `online` |

Commands are validated against the parameter table like every web/API write; invalid or
out-of-range values are ignored (logged on the serial console). Expert values (PID, brew PID,
detection) can only be changed in the web UI with the web password.

## 3. Entities

| Entity | Notes |
|---|---|
| `sensor.silvia_temperature` | °C |
| `sensor.silvia_heater` | heater power % |
| `sensor.silvia_state` | off / booting / heating / ready / shot / recovering / steam / error / standby |
| `binary_sensor.silvia_ready` | |
| `sensor.silvia_error` | none / sensor / overtemp |
| `sensor.silvia_last_shot` | duration of the last shot in s |
| `sensor.silvia_shot_count` | shots since the machine was switched on |
| `event.silvia_shot` | fires once per shot, attributes as in `silvia/event/shot` |
| `number.silvia_brew_setpoint`, `number.silvia_steam_setpoint`, `number.silvia_standby_min` | controls |
| `switch.silvia_steam`, `switch.silvia_pid` | controls |
| `sensor.silvia_wifi_signal`, `sensor.silvia_firmware`, `sensor.silvia_pid_*` | diagnostic, disabled by default |

## 4. Home Assistant configuration

Done in the HA UI (not in YAML):

- the kitchen plug `switch.sonoff_steckdose_kuche` and the Silvia device in the same area
  (Küche), so power draw and machine state sit side by side;
- a dashboard card with temperature, state, last shot, history and the plug's power;
- an automation "Silvia bereit": push to the phone when the machine first reaches the set point
  after being switched on;
- an automation "Silvia Fehler": push on `sensor.silvia_error` changing to `sensor` or
  `overtemp`.
