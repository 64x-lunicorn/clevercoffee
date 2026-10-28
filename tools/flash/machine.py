"""Reads version and settings from the Silvia, whichever firmware runs on it.

    python3 machine.py HOST            # JSON: {"version": ..., "params": {key: value}}
    python3 machine.py diff OLD NEW    # compare two such files, one line per setting

The fork's API (/api/state, /api/parameters) is used when present, otherwise the 3.3.0
/parameters endpoint, with its names mapped to the fork's keys.
"""
import json
import sys
import urllib.request

LEGACY = {  # 3.3.0 name -> fork key
    "PID_ON": "pid_on", "BREW_SET_POINT": "brew_setpoint", "STEAM_SET_POINT": "steam_setpoint",
    "BREW_TEMP_OFFSET": "temp_offset", "PID_KP": "pid_kp", "PID_TN": "pid_tn", "PID_TV": "pid_tv",
    "PID_I_MAX": "pid_i_max", "STEAM_KP": "steam_kp", "PID_BD_ON": "bd_pid_on", "PID_BD_KP": "bd_kp",
    "PID_BD_TN": "bd_tn", "PID_BD_TV": "bd_tv", "PID_BD_TIME": "bd_time",
    "PID_BD_BREWSENSITIVITY": "bd_sensitivity",
}


def get(host, path):
    with urllib.request.urlopen(f"http://{host}{path}", timeout=5) as r:
        return json.load(r)


def read(host):
    try:
        state = get(host, "/api/state")
        params = {p["key"]: p["value"] for p in get(host, "/api/parameters")}
        return {"version": state["version"], "params": params}
    except Exception:
        legacy = {p["name"]: p["value"] for p in get(host, "/parameters")}
        params = {LEGACY[k]: v for k, v in legacy.items() if k in LEGACY}
        return {"version": legacy["VERSION"], "params": params}


def norm(value):
    return float(value) if not isinstance(value, str) else value


def diff(old, new):
    same = True
    for key, before in old["params"].items():
        after = new["params"].get(key)
        if after is None:
            print(f"! {key}: {before} -> missing")
            same = False
        elif norm(after) == norm(before):
            print(f"= {key} = {after}")
        else:
            print(f"! {key}: {before} -> {after}")
            same = False
    return same


if __name__ == "__main__":
    if sys.argv[1] == "diff":
        with open(sys.argv[2]) as a, open(sys.argv[3]) as b:
            sys.exit(0 if diff(json.load(a), json.load(b)) else 1)
    try:
        print(json.dumps(read(sys.argv[1])))
    except Exception:
        sys.exit(1)
