"""Replays the firmware's software brew detection (BREWDETECTION 1) over logged
sensor data and checks: every shot is detected quickly, idle phases never trigger.

    python3 test_brew_detection.py [sensitivity ...]
"""
import json
import sys
from pathlib import Path

from silvia_sim import SoftwareBD

HERE = Path(__file__).parent
SETPOINT = 95.0
SAMPLE_S = 0.4  # intervaltempmestsic


def load(path):
    rows = sorted((json.loads(l) for l in open(path) if '"error"' not in l), key=lambda r: r["t"])
    t0 = rows[0]["t"]
    return [(r["t"] - t0, round(r["currentTemp"], 1)) for r in rows]


def shot_starts(series):
    """Water draws in the logs: a drop of 0.3 C or more within ~1 s (logger gaps skipped),
    at least 2 min apart."""
    starts = []
    for (t1, a), (t2, b) in zip(series, series[1:]):
        if t2 - t1 > 3:
            continue
        if b - a <= -0.3 and (not starts or t1 - starts[-1] > 120):
            starts.append(t1)
    return starts


def replay(series, sensitivity, bd_time=40):
    """Sample-and-hold the 1 Hz log at the firmware's 400 ms sensor interval."""
    bd = SoftwareBD(series[0][1])
    detections = []
    detected_at = None
    j = 0
    t = series[0][0]
    while t <= series[-1][0]:
        while j + 1 < len(series) and series[j + 1][0] <= t:
            j += 1
        temp = series[j][1]
        bd.add(t * 1000 + 2e6, temp)
        if detected_at is not None and t - detected_at > bd_time:
            detected_at = None
        if detected_at is None and bd.rate_avg <= -sensitivity and abs(temp - SETPOINT) < 5:
            detected_at = t
            detections.append(t)
        t += SAMPLE_S
    return detections


def main():
    sensitivities = [float(a) for a in sys.argv[1:]] or [60, 90, 120, 150, 200]
    logs = {p.stem: load(p) for p in sorted((HERE / "data").glob("shot*.jsonl"))}
    ok_all = True
    for sens in sensitivities:
        print(f"sensitivity {sens:g}:")
        for name, series in logs.items():
            starts = shot_starts(series)
            dets = replay(series, sens)
            hits, false = [], []
            for d in dets:
                near = [s for s in starts if -2 <= d - s <= 20]
                (hits if near else false).append((d, near[0] if near else None))
            missed = [s for s in starts if not any(-2 <= d - s <= 20 for d in dets)]
            lat = ", ".join(f"{d - s:.1f}s" for d, s in hits)
            idle_min = (series[-1][0] - series[0][0]) / 60
            ok = not missed and not false
            ok_all &= ok if sens == 120 else True
            print(f"  {name}: {len(starts)} shot(s), detected after {lat or '-'}; "
                  f"missed {len(missed)}, false triggers {len(false)} in {idle_min:.0f} min"
                  f"{'' if ok else '  <-- FAIL'}")
    sys.exit(0 if ok_all else 1)


if __name__ == "__main__":
    main()
