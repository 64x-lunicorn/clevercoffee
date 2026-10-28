# Silvia HTTP API

Base URL: `http://<machine>/` (hostname `silvia`). All bodies are JSON (UTF-8).

- **Reads** need no login.
- **Writes** (`POST`) need HTTP basic auth, user `silvia`, password set at flash time. Without
  it the answer is `401` with `WWW-Authenticate: Basic realm="silvia"`, so a browser asks once.
- Every write goes through the parameter table (`lib/silvia_core/src/Params.cpp`): unknown,
  read-only, unparsable or out-of-range values are rejected and **nothing** of that request is
  applied.
- Values are only written to flash when they actually change.

## `GET /api/state`

The whole "what is the machine doing" in one document.

```jsonc
{
  "temperature": 95.8,          // °C, null until the first valid sensor reading
  "setpoint": 96.0,             // active set point (steam set point in steam mode)
  "brew_setpoint": 96.0,
  "steam_setpoint": 120.0,
  "heater": 27.5,               // heater power in %
  "state": "ready",             // off | booting | heating | ready | shot | recovering | steam | error | standby
  "state_label": "Bereit",      // German, as on the display
  "ready": true,
  "error": "none",              // none | sensor | overtemp
  "pid_on": true,
  "steam_on": false,
  "brew_pid_active": false,
  "shot_timer_s": null,         // seconds since the (back-dated) shot start while state is "shot"
  "shot_count": 2,              // shots since boot
  "last_shot": {                // null until the first shot
    "id": 2,
    "start_uptime_s": 1834.2,
    "start_unix": 1793086098,   // null if the clock was not set (NTP)
    "duration_s": 32.7,
    "start_temp": 95.9,         // top of the drop
    "min_temp": 88.1,
    "peak_temp": 96.7,          // highest temperature in the 5 min after the shot (so far)
    "ready_after_s": 41,        // seconds from the end until "ready" again, null until then
    "complete": false           // peak and ready time are final (5 min after the shot)
  },
  "uptime_s": 1900,
  "wifi_rssi": -61,             // dBm, null if not connected
  "mqtt_connected": false,
  "free_heap": 31544,
  "version": "silvia-2"
}
```

### States

| state | meaning |
|---|---|
| `off` | PID switched off, heater off |
| `booting` | no valid sensor reading yet, heater off |
| `heating` | more than 0.5 °C below the set point |
| `ready` | within ±0.5 °C of the set point (stays ready within ±1 °C) |
| `shot` | a shot is running (detected from the temperature drop) |
| `recovering` | above the set point: overshoot after a shot or heat-up, or cooling down after steam |
| `standby` | no shot, steam or change for `standby_min` minutes: heater off until the power is cut and restored |
| `steam` | steam mode |
| `error` | heater off: `sensor` (implausible or no readings for 4–5 s) or `overtemp` (above 120 °C, 145 °C in steam mode; clears below brew set point + 5 °C) |

### Shot timing

Shots are detected about 4 s after the pump starts, from the falling temperature. The start is
back-dated to the first reading of the falling edge, the end is the first reading at the lowest
temperature (the moment the fall stops), so durations are accurate to about 1 s.

## `GET /api/parameters`

Array with one entry per setting:

```jsonc
[{"key": "brew_setpoint", "label": "Brühtemperatur", "help": "…", "type": "number", "unit": "°C",
  "value": 96.00, "min": 80.0, "max": 105.0, "step": 0.1, "default": 96.0,
  "expert": false, "persisted": true}, …]
```

| key | type | range | default | |
|---|---|---|---|---|
| `pid_on` | bool | | true | |
| `brew_setpoint` | number | 80–105 °C | 96 | |
| `steam_setpoint` | number | 100–140 °C | 120 | |
| `steam_on` | bool | | false | not persisted |
| `temp_offset` | number | 0–20 °C | 0 | expert; subtracted from every reading |
| `pid_kp` | number | 0–200 | 62 | expert |
| `pid_tn` | number | 0–999 s | 52 | expert, 0 = no I term |
| `pid_tv` | number | 0–999 s | 11.5 | expert |
| `pid_i_max` | number | 0–999 | 75 | expert, integrator limit (‰ heater) |
| `steam_kp` | number | 0–500 | 150 | expert |
| `bd_pid_on` | bool | | false | expert, brew PID |
| `bd_kp`, `bd_tn`, `bd_tv` | number | 0–200, 0–999, 0–999 | 50, 0, 20 | expert |
| `bd_time` | number | 1–180 s | 30 | expert, brew PID duration after detection |
| `bd_sensitivity` | number | 0–999 | 120 | expert, 0 = detection off |
| `standby_min` | number | 30–240 | 60 | whole minutes; standby after this long without a shot, steam or change. Cannot be switched off |

Booleans accept `true/false`, `1/0`, `"ON"/"OFF"`. The 3.3.0 names (`BREW_SET_POINT`, …) are
accepted as keys too. `temperature`, `version`, `state` and the other values of `/api/state`
are read-only.

## `POST /api/parameters` (auth)

Partial update:

```bash
curl -u silvia -H 'Content-Type: application/json' -d '{"brew_setpoint": 95.5}' http://silvia/api/parameters
```

- `200` with the full parameter list (as `GET`).
- `400` with per-field errors; nothing was applied:

  ```json
  {"errors": {"brew_setpoint": {"code": "out_of_range", "min": 80.0, "max": 105.0},
              "temperature": {"code": "read_only"}}}
  ```

  Codes: `unknown`, `read_only`, `invalid`, `out_of_range`.
- `401` without valid credentials, `415` if the body is not `application/json`.

## `POST /api/command` (auth)

```bash
curl -u silvia -H 'Content-Type: application/json' -d '{"steam": true}' http://silvia/api/command
```

Keys `steam` and `pid` (booleans). Answers with `/api/state` (200) or field errors (400).

## `GET /api/history`

The last 15 minutes at 1 s resolution, oldest first, as columns (about 14 KB, sent chunked):

```jsonc
{
  "interval_s": 1,
  "end_uptime_s": 1900,                 // uptime of the newest sample
  "states": ["off", "booting", "heating", "ready", "shot", "recovering", "steam", "error", "standby"],
  "temperature": [95.9, 96.0, …],       // °C, null while unknown
  "setpoint": [96.0, 96.0, …],
  "heater": [24.8, 26.0, …],            // %
  "state": [3, 3, …]                    // index into "states"
}
```

## `GET /api/shots`

The last 10 shot summaries (newest first, fields as `last_shot` above) and the 1 s curve of the
most recent shot from its back-dated start, up to 120 s:

```jsonc
{
  "shots": [{"id": 3, "duration_s": 32.7, …}, …],
  "last_curve": {"shot_id": 3, "start_uptime_s": 1834.2, "interval_s": 1,
                 "temperature": [95.9, 95.6, …], "heater": [24.0, 31.2, …]}   // null before the first shot
}
```

Shots are kept in RAM only: they are gone after a power cycle (long-term history lives in
Home Assistant).

## `GET /api/events`

Server-sent events (`EventSource`):

| event | data | when |
|---|---|---|
| `state` | the `/api/state` document | every second while a client listens |
| `shot_started` | `{"event", "uptime_s", "shot": {…}}` | a shot was detected (start already back-dated) |
| `shot_finished` | same | duration, start and minimum known |
| `shot_complete` | same | peak and time until ready final (5 min later) |
| `error_raised`, `error_cleared` | `{"event", "uptime_s", "error"}` | heater switched off / allowed again |
| `ready_reached` | `{"event", "uptime_s", "first_since_boot"}` | entered `ready`; `first_since_boot` marks the end of the heat-up |

## Web UI

`GET /` serves the web UI from LittleFS (`/www/index.html.gz`, built by `web/build.py` from
`web/`). It only talks to the endpoints above. Without the filesystem image a small built-in
status page is served instead.

For UI work without the machine: `python3 web/build.py && python3 web/mock_server.py` serves the
same API from the recorded shots on <http://localhost:8080> (writes: `silvia` / `test`).

## Removed

The 3.3.0 endpoints (`/parameters`, `/temperatures`, `/timeseries`, `/toggleSteam`,
`/togglePid`, `/events`) are gone.
