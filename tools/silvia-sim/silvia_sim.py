"""Thermal model of the Silvia boiler, fitted to logged shots, plus a replica
of the Clever Coffee 3.3.0 controller (PID lib + software brew detection).

Usage:
    python3 silvia_sim.py fit      # fit model to data/shot*.jsonl, write model.json
    python3 silvia_sim.py tune     # search brew-PID parameters in closed loop
"""
import json
import math
import sys
from pathlib import Path

import numpy as np

HERE = Path(__file__).parent
DT = 0.2  # simulation step (s)
T_AMB = 20.0
T_IN = 20.0


# ---------------------------------------------------------------- data

def load_shot(path, shot_len):
    rows = [json.loads(l) for l in open(path) if '"error"' not in l]
    rows.sort(key=lambda r: r["t"])
    t = np.array([r["t"] for r in rows])
    t -= t[0]
    temp = np.array([r["currentTemp"] for r in rows])
    heat = np.array([r["heaterPower"] for r in rows]) / 1000.0
    # shot start = last sample at/above (setpoint - 0.2) before the first steep drop
    drop = np.where((temp[1:] - temp[:-1]) <= -0.3)[0][0]
    start = t[drop]
    # only fit a window around the shot, open-loop drift over long idle periods hides the dynamics
    keep = (t >= start - 150) & (t <= start + 330)
    t, temp, heat = t[keep] - t[keep][0], temp[keep], heat[keep]
    start = start - (start - 150 if start >= 150 else 0)
    return {"name": Path(path).stem, "t": t, "temp": temp, "heat": heat,
            "shot_start": start, "shot_len": shot_len}


# ---------------------------------------------------------------- plant

class Plant:
    """Heater element -> boiler -> sensor, plus cold-water inflow during a shot."""

    def __init__(self, p, t_init):
        self.p = p
        self.th = t_init + p["th0"]  # element is a bit hotter than the boiler at idle
        self.tb = t_init
        self.ts = t_init

    def step(self, u, shot):
        p = self.p
        q = p["q"] if shot else 0.0
        # b: element->boiler exchange rate seen from the element, c: element/boiler heat capacity ratio
        flow = p["b"] * (self.th - self.tb)
        dth = p["a"] * u - flow
        dtb = p["c"] * flow - p["l"] * (self.tb - T_AMB) - q * (self.tb - T_IN)
        self.th += dth * DT
        self.tb += dtb * DT
        self.ts += (self.tb - self.ts) / p["tau"] * DT
        return self.ts


def replay(p, shot):
    """Open loop: feed the logged heater power, return simulated sensor temp at log times."""
    t, heat = shot["t"], shot["heat"]
    plant = Plant(p, shot["temp"][0])
    out = np.empty_like(t)
    sim_t = 0.0
    j = 0
    for i in range(len(t)):
        while sim_t < t[i]:
            u = heat[max(j - 1, 0)]
            in_shot = shot["shot_start"] <= sim_t < shot["shot_start"] + shot["shot_len"]
            plant.step(u, in_shot)
            sim_t += DT
        j = i + 1
        out[i] = plant.ts
    return out


PARAM_NAMES = ["a", "b", "c", "l", "q", "tau", "th0"]
BOUNDS = {"a": (0.05, 50), "b": (0.002, 5), "c": (0.005, 1), "l": (1e-5, 0.02),
          "q": (0.001, 0.2), "tau": (0.3, 40), "th0": (0, 60)}


def to_params(x):
    # optimise in log space so parameters of very different scale move sensibly
    return {k: float(np.clip(math.exp(v), *BOUNDS[k])) for k, v in zip(PARAM_NAMES, x)}


def nelder_mead(f, x0, step, iters=3000, tol=1e-7):
    n = len(x0)
    pts = [np.array(x0, float)]
    for i in range(n):
        x = np.array(x0, float)
        x[i] += step[i]
        pts.append(x)
    vals = [f(x) for x in pts]
    for _ in range(iters):
        order = np.argsort(vals)
        pts = [pts[i] for i in order]
        vals = [vals[i] for i in order]
        if abs(vals[-1] - vals[0]) < tol:
            break
        centroid = np.mean(pts[:-1], axis=0)
        xr = centroid + (centroid - pts[-1])
        fr = f(xr)
        if fr < vals[0]:
            xe = centroid + 2 * (centroid - pts[-1])
            fe = f(xe)
            pts[-1], vals[-1] = (xe, fe) if fe < fr else (xr, fr)
        elif fr < vals[-2]:
            pts[-1], vals[-1] = xr, fr
        else:
            xc = centroid + 0.5 * (pts[-1] - centroid)
            fc = f(xc)
            if fc < vals[-1]:
                pts[-1], vals[-1] = xc, fc
            else:
                for i in range(1, len(pts)):
                    pts[i] = pts[0] + 0.5 * (pts[i] - pts[0])
                    vals[i] = f(pts[i])
    i = int(np.argmin(vals))
    return pts[i], vals[i]


def fit(shots):
    def cost(x):
        p = to_params(x)
        err = 0.0
        for s in shots:
            sim = replay(p, s)
            err += float(np.mean((sim - s["temp"]) ** 2))
        # measured during this morning's cold start: ~0.52 C/s at 100 % around 60 C
        warm = p["c"] * p["a"] - p["l"] * (60 - T_AMB)
        return err / len(shots) + 20 * (warm - 0.52) ** 2

    best = None
    for start in ([1.0, 0.1, 0.05, 0.0005, 0.01, 3.0, 5.0],
                  [5.0, 0.5, 0.2, 0.001, 0.02, 8.0, 15.0],
                  [0.5, 0.05, 0.02, 0.0003, 0.005, 1.0, 2.0]):
        x0 = [math.log(v) for v in start]
        for _ in range(3):
            x, v = nelder_mead(cost, x0, [0.7] * len(x0), iters=1500)
            x0 = x
        if best is None or v < best[1]:
            best = (x, v)
    p = to_params(best[0])
    return p, math.sqrt(best[1])


# ---------------------------------------------------------------- controller

class PID:
    """Port of rancilio-pid/Arduino-PID-Library (P_ON_E path) as used by 3.3.0."""

    def __init__(self, out_max=1000.0, alpha=0.6):
        self.out_max = out_max
        self.alpha = alpha
        self.integrator = 0.0
        self.i_min, self.i_max = 0.0, out_max
        self.kp = self.ki = self.kd = 0.0
        self.last_filtered = None
        self.output = 0.0
        self.auto = True

    def set_tunings(self, kp, tn, tv):
        ki = kp / tn if tn else 0.0
        self.kp, self.ki, self.kd = kp, ki, tv * kp
        if ki == 0:
            self.integrator = 0.0

    def set_mode(self, auto, inp):
        if auto and not self.auto:
            self.integrator = min(max(self.output, 0.0), self.out_max)
            self.last_filtered = inp
        self.auto = auto

    def compute(self, inp, sp):
        if not self.auto:
            return self.output
        if self.last_filtered is None:
            self.last_filtered = inp
        err = sp - inp
        if 0.01 < self.output < self.out_max - 0.01:
            self.integrator += self.ki * err
        old = self.last_filtered
        self.last_filtered = self.alpha * self.last_filtered + (1 - self.alpha) * inp
        d_inp = self.last_filtered - old
        self.integrator = min(max(self.integrator, 0.0), self.out_max)
        self.integrator = min(max(self.integrator, self.i_min), self.i_max)
        out = self.kp * err + self.integrator - self.kd * d_inp
        self.output = min(max(out, 0.0), self.out_max)
        return self.output


class SoftwareBD:
    """calculateTemperatureMovingAverage() + brewDetection() for mode 1."""

    def __init__(self, t0):
        self.n = 15
        self.vals = [t0] * self.n
        self.times = [0.0] * self.n
        self.rates = [0.0] * self.n
        self.idx = 1
        self.rate_avg = 0.0

    def add(self, now_ms, temp):
        n, i = self.n, self.idx
        self.times[i] = now_ms
        self.vals[i] = temp
        j = 0 if i == n - 1 else i + 1
        dt = self.times[i] - self.times[j]
        rate = (self.vals[i] - self.vals[j]) / dt * 10000 if dt else 0.0
        self.rates[i] = rate
        self.rate_avg = sum(self.rates) / n * 100
        self.idx = 0 if i >= n - 1 else i + 1


def closed_loop(p, cfg, shot_at=300.0, shot_len=35.0, total=900.0, sp=95.0):
    """Simulate one shot with the firmware logic. cfg keys:
    bd (bool), bd_kp, bd_tn, bd_tv, bd_time, sens, delay; normal kp, tn, tv, imax."""
    plant = Plant(p, sp)
    plant.th = sp + p["th0"] * 0.35  # rough idle equilibrium, settled below
    pid = PID()
    pid.set_tunings(cfg["kp"], cfg["tn"], cfg["tv"])
    pid.i_max = cfg["imax"]
    bd = SoftwareBD(sp)
    # settle at idle for 20 min before the shot
    t = -1200.0
    next_temp, next_pid = t, t
    sensor = sp
    detected_at = None
    paused = False
    log = []
    while t < total:
        in_shot = shot_at <= t < shot_at + shot_len
        true_ts = plant.step(pid.output / 1000.0, in_shot)
        if t >= next_temp:
            sensor = round(true_ts, 1)  # TSIC resolution
            next_temp += 0.4
            if cfg["bd"]:
                bd.add(t * 1000 + 2e6, sensor)
                if (detected_at is None and bd.rate_avg <= -cfg["sens"]
                        and abs(sensor - sp) < 5):
                    detected_at = t
            if detected_at is not None and t - detected_at > cfg["bd_time"]:
                detected_at = None
                paused = False
                pid.set_mode(True, sensor)
        if t >= next_pid:
            next_pid += 1.0
            if detected_at is not None:
                brewed = t - detected_at
                if cfg["delay"] > 0 and 0 < brewed < cfg["delay"]:
                    if not paused:
                        paused = True
                        pid.set_mode(False, sensor)
                else:
                    if paused:
                        paused = False
                        pid.set_mode(True, sensor)
                    if cfg["use_bd_pid"]:
                        pid.set_tunings(cfg["bd_kp"], cfg["bd_tn"], cfg["bd_tv"])
                    else:
                        pid.set_tunings(cfg["kp"], cfg["tn"], cfg["tv"])
            else:
                pid.set_tunings(cfg["kp"], cfg["tn"], cfg["tv"])
                pid.i_max = cfg["imax"]
            pid.compute(sensor, sp)
        if t >= shot_at - 30 and abs((t * 5) - round(t * 5)) < 1e-6 and abs(t - round(t)) < 1e-6:
            log.append((t - shot_at, sensor, pid.output, detected_at is not None))
        t += DT
    return log


def score(log, sp=95.0):
    temps = np.array([r[1] for r in log])
    ts = np.array([r[0] for r in log])
    shot = (ts >= 0) & (ts < 40)
    after = ts >= 40
    drop = sp - temps[shot].min()
    over = max(temps[after].max() - sp, 0.0)
    # time until back inside +/-0.3 C for good
    outside = np.where(np.abs(temps - sp) > 0.3)[0]
    settle = ts[outside[-1]] if len(outside) else 0.0
    ise = float(np.sum((temps[ts >= 0] - sp) ** 2))
    return {"drop": round(drop, 2), "overshoot": round(over, 2),
            "settle_s": round(settle), "ise": round(ise)}


CURRENT = {"kp": 62, "tn": 52, "tv": 11.5, "imax": 75,
           "bd": False, "use_bd_pid": False, "bd_kp": 120, "bd_tn": 0, "bd_tv": 20,
           "bd_time": 40, "sens": 120, "delay": 10}


def main():
    cmd = sys.argv[1] if len(sys.argv) > 1 else "fit"
    if cmd == "fit":
        shots = [load_shot(HERE / "data/shot1.jsonl", 40), load_shot(HERE / "data/shot2.jsonl", 35)]
        p, rmse = fit(shots)
        json.dump({"params": p, "rmse": rmse}, open(HERE / "model.json", "w"), indent=2)
        print("fit rmse %.3f C" % rmse, json.dumps(p))
        for s in shots:
            sim = replay(p, s)
            print(s["name"], "rmse %.3f" % math.sqrt(np.mean((sim - s["temp"]) ** 2)),
                  "min real %.1f sim %.1f" % (s["temp"].min(), sim.min()),
                  "max-after real %.1f sim %.1f" % (
                      s["temp"][s["t"] > s["shot_start"] + 40].max(),
                      sim[s["t"] > s["shot_start"] + 40].max()))
    elif cmd == "tune":
        p = json.load(open(HERE / "model.json"))["params"]
        print("current firmware (no BD):", score(closed_loop(p, CURRENT)))
        results = []
        for delay in (0, 10):
            for bd_kp in (50, 80, 120, 160, 200):
                for bd_tv in (0, 10, 20, 40):
                    for bd_time in (20, 25, 30, 35, 40):
                        cfg = dict(CURRENT, bd=True, use_bd_pid=True, bd_kp=bd_kp,
                                   bd_tv=bd_tv, bd_time=bd_time, delay=delay)
                        s = score(closed_loop(p, cfg))
                        results.append((s["ise"], cfg, s))
        results.sort(key=lambda r: r[0])
        for ise, cfg, s in results[:10]:
            print({k: cfg[k] for k in ("delay", "bd_kp", "bd_tv", "bd_time")}, s)
        cfg = dict(CURRENT, bd=True, use_bd_pid=True)
        print("BD as configured now (Kp120 Tv20 40s delay10):", score(closed_loop(p, cfg)))


if __name__ == "__main__":
    main()
