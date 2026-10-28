"""A fake Silvia for developing the web UI: serves data/www/index.html.gz and the HTTP API
(docs/api.md, incl. the event stream) from the recorded shots in tools/silvia-sim/data.

    python3 web/build.py && python3 web/mock_server.py [--port 8080] [--speed 1]

Writes need basic auth silvia:test. The replay starts 60 s before the first shot of
shot1.jsonl and loops; --speed 5 plays it five times faster.
"""
import argparse
import base64
import json
import threading
import time
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
LOG = ROOT / "tools" / "silvia-sim" / "data" / "shot1.jsonl"
STATES = ["off", "booting", "heating", "ready", "shot", "recovering", "steam", "error"]
LABELS = {"off": "Aus", "booting": "Start", "heating": "Aufheizen", "ready": "Bereit", "shot": "Bezug",
          "recovering": "Nachschwingen", "steam": "Dampf", "error": "Fehler"}
AUTH = "Basic " + base64.b64encode(b"silvia:test").decode()

PARAMS = [
    # key, label, type, unit, min, max, step, default, expert
    ("pid_on", "Regelung an", "bool", "", 0, 1, 1, 1, False),
    ("brew_setpoint", "Brühtemperatur", "number", "°C", 80, 105, 0.1, 96, False),
    ("steam_setpoint", "Dampftemperatur", "number", "°C", 100, 140, 0.5, 120, False),
    ("steam_on", "Dampfmodus", "bool", "", 0, 1, 1, 0, False),
    ("temp_offset", "Temperatur-Offset", "number", "°C", 0, 20, 0.1, 0, True),
    ("pid_kp", "PID Kp", "number", "", 0, 200, 0.1, 62, True),
    ("pid_tn", "PID Tn", "number", "s", 0, 999, 0.1, 52, True),
    ("pid_tv", "PID Tv", "number", "s", 0, 999, 0.1, 11.5, True),
    ("pid_i_max", "PID I-Max", "number", "", 0, 999, 1, 75, True),
    ("steam_kp", "Dampf Kp", "number", "", 0, 500, 1, 150, True),
    ("bd_pid_on", "Brüh-PID", "bool", "", 0, 1, 1, 0, True),
    ("bd_kp", "Brüh-PID Kp", "number", "", 0, 200, 0.1, 120, True),
    ("bd_tn", "Brüh-PID Tn", "number", "s", 0, 999, 0.1, 0, True),
    ("bd_tv", "Brüh-PID Tv", "number", "s", 0, 999, 0.1, 20, True),
    ("bd_time", "Brüh-PID-Zeit", "number", "s", 1, 180, 1, 30, True),
    ("bd_sensitivity", "Erkennungs-Empfindlichkeit", "number", "", 0, 999, 1, 120, True),
]


def load_log():
    rows = sorted((json.loads(l) for l in open(LOG) if '"error"' not in l), key=lambda r: r["t"])
    t0 = rows[0]["t"]
    out, last = [], None
    for r in rows:  # resample to whole seconds
        s = int(r["t"] - t0)
        if s != last:
            out.append((round(r["currentTemp"], 1), round(r["heaterPower"] / 10, 1)))
            last = s
    return out


class Machine:
    def __init__(self, speed):
        self.log = load_log()
        self.speed = speed
        self.values = {p[0]: p[7] for p in PARAMS}
        self.lock = threading.Lock()
        self.history = []  # (temp, setpoint, heater, state)
        self.shots = []
        self.curve = None
        self.shot_start = None
        self.pos = 0
        self.uptime = 3600
        self.listeners = []
        for _ in range(900):  # pre-fill 15 min
            self.step(silent=True)
        self.pos = 0

    def state_name(self, temp, i):
        rising = i > 3 and self.log[i - 1][0] - self.log[i - 4][0] > 0
        falling = i > 3 and self.log[i - 4][0] - self.log[i][0] > 1.0
        if not self.values["pid_on"]:
            return "off"
        if self.values["steam_on"]:
            return "steam"
        if self.shot_start is not None and not rising:
            return "shot"
        if falling and abs(temp - self.values["brew_setpoint"]) < 5 and self.shot_start is None:
            return "shot"
        d = temp - self.values["brew_setpoint"]
        return "ready" if abs(d) <= 1 else "heating" if d < 0 else "recovering"

    def step(self, silent=False):
        with self.lock:
            temp, heater = self.log[self.pos % len(self.log)]
            state = self.state_name(temp, self.pos % len(self.log))
            events = []
            if state == "shot" and self.shot_start is None:
                self.shot_start = self.uptime - 4
                self.curve = {"shot_id": len(self.shots) + 1, "start_uptime_s": self.shot_start, "interval_s": 1,
                              "temperature": [h[0] for h in self.history[-4:]], "heater": [h[2] for h in self.history[-4:]]}
                events.append(("shot_started", {"event": "shot_started", "uptime_s": self.uptime}))
            if state != "shot" and self.shot_start is not None:
                curve_temps = self.curve["temperature"]
                shot = {"id": len(self.shots) + 1, "start_uptime_s": self.shot_start,
                        "start_unix": int(time.time()) - (self.uptime - self.shot_start),
                        "duration_s": float(self.uptime - self.shot_start - 2), "start_temp": curve_temps[0],
                        "min_temp": min(curve_temps), "peak_temp": temp, "ready_after_s": None, "complete": False}
                self.shots.insert(0, shot)
                self.shots = self.shots[:10]
                self.shot_start = None
                events.append(("shot_finished", {"event": "shot_finished", "uptime_s": self.uptime, "shot": shot}))
            if self.curve and len(self.curve["temperature"]) < 120:
                self.curve["temperature"].append(temp)
                self.curve["heater"].append(heater)
            self.history.append((temp, self.values["brew_setpoint"], heater, STATES.index(state)))
            self.history = self.history[-900:]
            self.pos += 1
            self.uptime += 1
            snapshot = self.state()
        if not silent:
            for q in list(self.listeners):
                q.append(("state", snapshot))
                q.extend(events)

    def state(self):
        temp, sp, heater, st = self.history[-1]
        name = STATES[st]
        return {
            "temperature": temp, "setpoint": self.values["steam_setpoint"] if self.values["steam_on"] else sp,
            "brew_setpoint": self.values["brew_setpoint"], "steam_setpoint": self.values["steam_setpoint"],
            "heater": heater, "state": name, "state_label": LABELS[name], "ready": name == "ready",
            "error": "none", "pid_on": bool(self.values["pid_on"]), "steam_on": bool(self.values["steam_on"]),
            "brew_pid_active": False,
            "shot_timer_s": float(self.uptime - self.shot_start) if self.shot_start is not None else None,
            "shot_count": len(self.shots), "last_shot": self.shots[0] if self.shots else None,
            "uptime_s": self.uptime, "wifi_rssi": -58, "mqtt_connected": False, "free_heap": 30120,
            "version": "silvia-mock",
        }

    def history_doc(self):
        with self.lock:
            h = list(self.history)
        return {"interval_s": 1, "end_uptime_s": self.uptime, "states": STATES,
                "temperature": [x[0] for x in h], "setpoint": [x[1] for x in h],
                "heater": [x[2] for x in h], "state": [x[3] for x in h]}

    def params_doc(self):
        return [{"key": k, "label": label, "help": f"Hilfetext für {label}.", "type": typ, "unit": unit,
                 "value": bool(self.values[k]) if typ == "bool" else self.values[k], "min": lo, "max": hi,
                 "step": step, "default": default, "expert": expert, "persisted": k != "steam_on"}
                for k, label, typ, unit, lo, hi, step, default, expert in PARAMS]

    def set_params(self, body):
        defs = {p[0]: p for p in PARAMS}
        errors, writes = {}, {}
        for k, v in body.items():
            if k in ("temperature", "version", "state"):
                errors[k] = {"code": "read_only"}
            elif k not in defs:
                errors[k] = {"code": "unknown"}
            elif isinstance(v, bool) or isinstance(v, (int, float)):
                v = float(v)
                if not defs[k][4] <= v <= defs[k][5]:
                    errors[k] = {"code": "out_of_range", "min": float(defs[k][4]), "max": float(defs[k][5])}
                else:
                    writes[k] = v
            else:
                errors[k] = {"code": "invalid"}
        if not errors:
            with self.lock:
                self.values.update(writes)
        return errors


class Handler(BaseHTTPRequestHandler):
    machine: Machine = None

    def log_message(self, fmt, *args):
        pass

    def send(self, code, body, ctype="application/json", extra=None):
        data = body if isinstance(body, bytes) else json.dumps(body).encode()
        self.send_response(code)
        self.send_header("Content-Type", ctype)
        self.send_header("Content-Length", str(len(data)))
        self.send_header("Cache-Control", "no-store")
        for k, v in (extra or {}).items():
            self.send_header(k, v)
        self.end_headers()
        self.wfile.write(data)

    def do_GET(self):
        m = self.machine
        if self.path in ("/", "/index.html"):
            gz = (ROOT / "data" / "www" / "index.html.gz").read_bytes()
            return self.send(200, gz, "text/html; charset=utf-8", {"Content-Encoding": "gzip"})
        if self.path == "/api/state":
            with m.lock:
                return self.send(200, m.state())
        if self.path == "/api/history":
            return self.send(200, m.history_doc())
        if self.path == "/api/shots":
            with m.lock:
                return self.send(200, {"shots": m.shots, "last_curve": m.curve})
        if self.path == "/api/parameters":
            return self.send(200, m.params_doc())
        if self.path == "/api/events":
            return self.events()
        self.send(404, {"error": "not_found"})

    def events(self):
        self.send_response(200)
        self.send_header("Content-Type", "text/event-stream")
        self.send_header("Cache-Control", "no-cache")
        self.end_headers()
        queue = []
        self.machine.listeners.append(queue)
        try:
            while True:
                while queue:
                    name, data = queue.pop(0)
                    self.wfile.write(f"event: {name}\ndata: {json.dumps(data)}\n\n".encode())
                    self.wfile.flush()
                time.sleep(0.1)
        except (BrokenPipeError, ConnectionResetError):
            pass
        finally:
            self.machine.listeners.remove(queue)

    def do_POST(self):
        if self.headers.get("Authorization") != AUTH:
            return self.send(401, b"", "text/plain", {"WWW-Authenticate": 'Basic realm="silvia"'})
        length = int(self.headers.get("Content-Length", 0))
        try:
            body = json.loads(self.rfile.read(length) or b"null")
        except ValueError:
            body = None
        if not isinstance(body, dict):
            return self.send(400, {"error": "expected_object"})
        m = self.machine
        if self.path == "/api/parameters":
            errors = m.set_params(body)
            return self.send(400, {"errors": errors}) if errors else self.send(200, m.params_doc())
        if self.path == "/api/command":
            mapped = {{"steam": "steam_on", "pid": "pid_on"}.get(k, k): v for k, v in body.items()}
            errors = m.set_params(mapped)
            with m.lock:
                return self.send(400, {"errors": errors}) if errors else self.send(200, m.state())
        self.send(404, {"error": "not_found"})


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--port", type=int, default=8080)
    ap.add_argument("--speed", type=float, default=1)
    ap.add_argument("--host", default="127.0.0.1", help="0.0.0.0 to reach it from a container")
    args = ap.parse_args()
    machine = Machine(args.speed)
    Handler.machine = machine

    def run():
        while True:
            machine.step()
            time.sleep(1 / args.speed)

    threading.Thread(target=run, daemon=True).start()
    print(f"mock Silvia on http://{args.host}:{args.port} (writes: silvia/test)")
    ThreadingHTTPServer((args.host, args.port), Handler).serve_forever()


if __name__ == "__main__":
    main()
