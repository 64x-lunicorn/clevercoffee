// Silvia web UI: live status, shot timer, history chart, quick controls, shot list, expert
// parameters. Talks only to the machine's own API (docs/api.md). Preact without a build step.
(function () {
  'use strict';
  const { h, render } = preact;
  const { useState, useEffect, useRef, useCallback } = preactHooks;

  const HISTORY_LEN = 900;
  const STALE_MS = 5000;

  const ERRORS = {
    sensor: ['Fühler-Fehler – Heizung aus',
      'Der Temperaturfühler liefert keine plausiblen Werte. Maschine ausschalten, Fühler und Kabel prüfen. ' +
      'Die Heizung bleibt aus, bis 10 s lang wieder gültige Werte kommen.'],
    overtemp: ['Zu heiß – Heizung aus',
      'Die Temperatur liegt über der Sicherheitsgrenze (120 °C, im Dampfmodus 145 °C). Die Heizung bleibt aus, ' +
      'bis die Temperatur unter Solltemperatur + 5 °C gefallen ist. Kommt das wieder, Maschine ausschalten.'],
  };

  // ------------------------------------------------------------------ API

  async function getJson(path) {
    const r = await fetch(path, { cache: 'no-store' });
    if (!r.ok) throw new Error(path + ': ' + r.status);
    return r.json();
  }

  // The browser asks for the web password (user "silvia") on the first 401.
  async function postJson(path, body) {
    const r = await fetch(path, {
      method: 'POST', headers: { 'Content-Type': 'application/json' }, body: JSON.stringify(body),
    });
    let data = null;
    try { data = await r.json(); } catch (e) { /* empty body */ }
    return { ok: r.ok, status: r.status, data };
  }

  function errorText(res) {
    if (res.status === 401) return 'Passwort falsch oder abgebrochen – nichts geändert.';
    if (res.data && res.data.errors) {
      return Object.entries(res.data.errors).map(([k, e]) =>
        k + ': ' + (e.code === 'out_of_range' ? 'erlaubt ' + e.min + '–' + e.max : e.code)).join(', ');
    }
    return 'Fehler ' + res.status;
  }

  // ------------------------------------------------------------------ helpers

  const fmt = (v, d = 1) => (v == null || Number.isNaN(v) ? '–' : v.toFixed(d).replace('.', ','));
  const durClass = (s) => (s > 30 ? 'long' : s >= 25 ? 'good' : 'short');
  const timerClass = (s) => (s > 30 ? 'toolong' : s >= 25 ? 'highlight' : '');

  function clock(unix) {
    if (!unix) return '';
    const d = new Date(unix * 1000);
    return d.toLocaleTimeString('de-DE', { hour: '2-digit', minute: '2-digit' });
  }

  function cssVar(name) {
    return getComputedStyle(document.documentElement).getPropertyValue(name).trim();
  }

  // ------------------------------------------------------------------ chart

  // series: {temperature[], setpoint[], heater[], shot[] (bool)}, one value per second
  function drawChart(canvas, series, opts) {
    const dpr = window.devicePixelRatio || 1;
    const w = canvas.clientWidth, hgt = canvas.clientHeight;
    if (!w || !hgt) return;
    canvas.width = w * dpr;
    canvas.height = hgt * dpr;
    const ctx = canvas.getContext('2d');
    ctx.scale(dpr, dpr);
    ctx.clearRect(0, 0, w, hgt);

    const n = series.temperature.length;
    if (n < 2) return;
    const padL = 34, padR = 30, padT = 8, padB = 20;
    const cw = w - padL - padR, ch = hgt - padT - padB;
    const temps = series.temperature.concat(series.setpoint || []).filter((v) => v != null);
    let lo = Math.min(...temps), hi = Math.max(...temps);
    if (hi - lo < 4) { const m = (hi + lo) / 2; lo = m - 2; hi = m + 2; }
    lo = Math.floor(lo - 0.5); hi = Math.ceil(hi + 0.5);
    const x = (i) => padL + (i / (opts.span - 1)) * cw + (opts.span - n) / (opts.span - 1) * cw;
    const y = (v) => padT + (1 - (v - lo) / (hi - lo)) * ch;
    const yh = (p) => padT + (1 - p / 100) * ch;

    ctx.font = '11px -apple-system, sans-serif';
    ctx.fillStyle = cssVar('--muted');
    ctx.strokeStyle = cssVar('--line');
    ctx.lineWidth = 1;
    const step = hi - lo > 20 ? 10 : hi - lo > 8 ? 2 : 1;
    for (let v = Math.ceil(lo / step) * step; v <= hi; v += step) {
      ctx.beginPath(); ctx.moveTo(padL, y(v)); ctx.lineTo(w - padR, y(v)); ctx.stroke();
      ctx.fillText(String(v), 4, y(v) + 4);
    }
    ctx.fillText('100%', w - padR + 2, padT + 8);
    if (opts.xLabels) {
      ctx.fillText(opts.xLabels[0], padL, hgt - 4);
      const t = opts.xLabels[1];
      ctx.fillText(t, w - padR - ctx.measureText(t).width, hgt - 4);
    }

    if (series.shot) {  // shot periods shaded
      ctx.fillStyle = cssVar('--shot');
      for (let i = 0; i < n; i++) {
        if (!series.shot[i]) continue;
        let j = i; while (j < n && series.shot[j]) j++;
        ctx.fillRect(x(i), padT, x(j - 1) - x(i) + 1, ch);
        i = j;
      }
    }

    ctx.fillStyle = cssVar('--heater');  // heater power as area
    ctx.beginPath(); ctx.moveTo(x(0), yh(0));
    for (let i = 0; i < n; i++) ctx.lineTo(x(i), yh(series.heater[i] || 0));
    ctx.lineTo(x(n - 1), yh(0)); ctx.closePath(); ctx.fill();

    const line = (values, color, dash, width) => {
      ctx.strokeStyle = color; ctx.lineWidth = width; ctx.setLineDash(dash);
      ctx.beginPath();
      let pen = false;
      for (let i = 0; i < n; i++) {
        const v = values[i];
        if (v == null) { pen = false; continue; }
        if (pen) ctx.lineTo(x(i), y(v)); else ctx.moveTo(x(i), y(v));
        pen = true;
      }
      ctx.stroke(); ctx.setLineDash([]);
    };
    if (series.setpoint) line(series.setpoint, cssVar('--setpoint'), [5, 4], 1.2);
    line(series.temperature, cssVar('--temp'), [], 2);
  }

  function Chart({ series, span, height, xLabels }) {
    const ref = useRef(null);
    useEffect(() => {
      const draw = () => ref.current && drawChart(ref.current, series, { span, xLabels });
      draw();
      window.addEventListener('resize', draw);
      const mq = window.matchMedia('(prefers-color-scheme: dark)');
      mq.addEventListener && mq.addEventListener('change', draw);
      return () => {
        window.removeEventListener('resize', draw);
        mq.removeEventListener && mq.removeEventListener('change', draw);
      };
    });
    return h('canvas', { ref, style: height ? { height: height + 'px' } : null });
  }

  // ------------------------------------------------------------------ parts

  function Banner({ state, stale }) {
    if (stale) {
      return h('div', { class: 'banner stale' }, 'Keine Verbindung zur Maschine',
        h('small', null, 'Die Werte unten sind veraltet. Ist die Maschine an und im WLAN?'));
    }
    if (state && state.error !== 'none' && ERRORS[state.error]) {
      const [title, text] = ERRORS[state.error];
      return h('div', { class: 'banner error' }, title, h('small', null, text));
    }
    return null;
  }

  function Status({ state }) {
    if (!state) return h('div', { class: 'card' }, 'Lade…');
    if (state.state === 'shot' && state.shot_timer_s != null) {
      return h('div', { class: 'card' },
        h('div', { class: 'timer ' + timerClass(state.shot_timer_s) },
          h('div', { class: 'big' }, Math.floor(state.shot_timer_s)),
          h('div', { class: 'label' }, 'Sekunden · ' + fmt(state.temperature) + ' °C')));
    }
    return h('div', { class: 'card status' },
      h('div', { class: 'temp' }, fmt(state.temperature), h('span', { class: 'unit' }, '°C')),
      h('div', { style: { textAlign: 'right' } },
        state.ready ? h('div', { class: 'ready' }, h('span', { class: 'dot' }), 'Bereit') : null,
        h('div', { class: 'state' }, state.state_label)),
      h('div', { class: 'meta' }, 'Soll ' + fmt(state.setpoint) + ' °C · Heizung ' + fmt(state.heater, 0) + ' %'),
      h('div', { class: 'meta', style: { textAlign: 'right' } },
        state.last_shot ? 'Letzter Shot ' + fmt(state.last_shot.duration_s) + ' s' : ''));
  }

  function Controls({ state, onChanged }) {
    const [draft, setDraft] = useState(null);
    const [msg, setMsg] = useState(['', false]);
    const [busy, setBusy] = useState(false);
    if (!state) return null;
    const current = state.brew_setpoint;
    const value = draft == null ? current : draft;

    const save = async () => {
      setBusy(true);
      const res = await postJson('/api/parameters', { brew_setpoint: Math.round(value * 10) / 10 });
      setBusy(false);
      if (res.ok) { setDraft(null); setMsg(['Gespeichert', false]); onChanged(); }
      else setMsg([errorText(res), true]);
    };
    const steam = async () => {
      setBusy(true);
      const res = await postJson('/api/command', { steam: !state.steam_on });
      setBusy(false);
      setMsg(res.ok ? ['', false] : [errorText(res), true]);
      onChanged();
    };

    return h('div', { class: 'card' },
      h('div', { class: 'controls' },
        h('div', null,
          h('h2', null, 'Brühtemperatur'),
          h('div', { class: 'stepper' },
            h('button', { class: 'round', onClick: () => setDraft(Math.max(80, value - 0.5)), 'aria-label': 'kälter' }, '−'),
            h('div', { class: 'value' }, fmt(value) + ' °C'),
            h('button', { class: 'round', onClick: () => setDraft(Math.min(105, value + 0.5)), 'aria-label': 'wärmer' }, '+')),
          draft != null && draft !== current
            ? h('div', { style: { display: 'flex', gap: '8px', marginTop: '8px' } },
              h('button', { class: 'primary', disabled: busy, onClick: save, style: { flex: 1 } }, 'Speichern'),
              h('button', { onClick: () => setDraft(null) }, 'Abbrechen'))
            : null),
        h('div', null,
          h('h2', null, 'Dampf'),
          h('div', { class: 'switch' },
            h('span', null, state.steam_on ? 'an · ' + fmt(state.steam_setpoint, 0) + ' °C' : 'aus'),
            h('button', {
              class: 'toggle' + (state.steam_on ? ' on' : ''), disabled: busy, onClick: steam,
              role: 'switch', 'aria-checked': state.steam_on, 'aria-label': 'Dampfmodus',
            })))),
      h('div', { class: 'msg' + (msg[1] ? ' err' : '') }, msg[0]));
  }

  function History({ history }) {
    if (!history) return null;
    const minutes = Math.round(history.temperature.length / 60);
    return h('div', { class: 'card' },
      h('h2', null, 'Verlauf'),
      h(Chart, { series: history, span: HISTORY_LEN, xLabels: ['−15 min', 'jetzt'] }),
      h('div', { class: 'legend' },
        h('span', null, h('i', { style: { background: cssVar('--temp') } }), 'Temperatur'),
        h('span', null, h('i', { style: { background: cssVar('--setpoint') } }), 'Soll'),
        h('span', null, h('i', { style: { background: cssVar('--heater'), height: '8px' } }), 'Heizung'),
        h('span', null, h('i', { style: { background: cssVar('--shot'), height: '8px' } }), 'Shot'),
        minutes < 15 ? h('span', null, minutes + ' min seit Start') : null));
  }

  function Shots({ shots }) {
    const [open, setOpen] = useState(null);
    if (!shots) return null;
    const list = shots.shots;
    return h('div', { class: 'card' },
      h('h2', null, 'Shots'),
      list.length === 0 ? h('div', { class: 'meta' }, 'Noch kein Shot seit dem Einschalten.') : null,
      h('ul', { class: 'shots' }, list.map((s) => {
        const curve = shots.last_curve && shots.last_curve.shot_id === s.id ? shots.last_curve : null;
        return h('li', { key: s.id },
          h('div', { class: 'shot-row', onClick: () => setOpen(open === s.id ? null : s.id) },
            h('span', { class: 'meta' }, '#' + s.id),
            h('span', { class: 'dur ' + durClass(s.duration_s) }, fmt(s.duration_s) + ' s'),
            h('span', { class: 'meta' },
              (clock(s.start_unix) ? clock(s.start_unix) + ' · ' : '') +
              fmt(s.start_temp) + ' → ' + fmt(s.min_temp) + ' °C')),
          open === s.id ? h('div', { class: 'shot-detail' },
            h('div', null, 'Start ' + fmt(s.start_temp) + ' °C, Minimum ' + fmt(s.min_temp) + ' °C, ' +
              'danach höchstens ' + fmt(s.peak_temp) + ' °C' +
              (s.ready_after_s != null ? ', wieder bereit nach ' + Math.round(s.ready_after_s) + ' s' : '') +
              (s.complete ? '' : ' (läuft noch)')),
            curve ? h(Chart, {
              series: { temperature: curve.temperature, heater: curve.heater },
              span: curve.temperature.length, height: 140, xLabels: ['0 s', curve.temperature.length + ' s'],
            }) : h('div', null, 'Kurve nur für den letzten Shot gespeichert.')) : null);
      })));
  }

  function Param({ p, value, error, onInput }) {
    if (p.type === 'bool') {
      return h('div', { class: 'param' },
        h('label', null, p.label,
          h('button', {
            class: 'toggle' + (value ? ' on' : ''), role: 'switch', 'aria-checked': !!value, 'aria-label': p.label,
            onClick: () => onInput(p.key, !value),
          })),
        h('div', { class: 'help' }, p.help),
        error ? h('div', { class: 'err' }, error) : null);
    }
    return h('div', { class: 'param' },
      h('label', { for: 'p-' + p.key }, p.label,
        h('span', { class: 'meta' }, p.min + '–' + p.max + (p.unit ? ' ' + p.unit : ''))),
      h('div', { class: 'help' }, p.help),
      h('input', {
        id: 'p-' + p.key, type: 'number', inputmode: 'decimal', min: p.min, max: p.max, step: p.step,
        value, class: error ? 'invalid' : '', onInput: (e) => onInput(p.key, e.target.value),
      }),
      error ? h('div', { class: 'err' }, error) : null);
  }

  function Expert({ params, onSaved }) {
    const [edits, setEdits] = useState({});
    const [errors, setErrors] = useState({});
    const [msg, setMsg] = useState(['', false]);
    if (!params) return null;
    const expert = params.filter((p) => p.expert);
    const changed = Object.keys(edits).length > 0;

    const save = async () => {
      const body = {};
      for (const [k, v] of Object.entries(edits)) body[k] = typeof v === 'boolean' ? v : Number(String(v).replace(',', '.'));
      const res = await postJson('/api/parameters', body);
      if (res.ok) {
        setEdits({}); setErrors({}); setMsg(['Gespeichert', false]); onSaved(res.data);
      } else {
        const errs = {};
        if (res.data && res.data.errors) {
          for (const [k, e] of Object.entries(res.data.errors)) {
            errs[k] = e.code === 'out_of_range' ? 'Erlaubt: ' + e.min + '–' + e.max : 'Ungültiger Wert';
          }
        }
        setErrors(errs); setMsg([errorText(res), true]);
      }
    };

    return h('div', { class: 'card' },
      h('details', null,
        h('summary', null, 'Experten-Einstellungen'),
        h('div', { class: 'help meta', style: { margin: '6px 0 4px' } },
          'PID-Regler, Brüh-PID und Shot-Erkennung. Änderungen gelten sofort und werden gespeichert.'),
        expert.map((p) => h(Param, {
          key: p.key, p, error: errors[p.key],
          value: p.key in edits ? edits[p.key] : p.value,
          onInput: (k, v) => setEdits(Object.assign({}, edits, { [k]: v })),
        })),
        h('div', { style: { display: 'flex', gap: '8px', marginTop: '12px' } },
          h('button', { class: 'primary', disabled: !changed, onClick: save }, 'Speichern'),
          h('button', { disabled: !changed, onClick: () => { setEdits({}); setErrors({}); } }, 'Verwerfen')),
        h('div', { class: 'msg' + (msg[1] ? ' err' : '') }, msg[0])));
  }

  // ------------------------------------------------------------------ app

  function toSeries(doc) {
    return {
      temperature: doc.temperature, setpoint: doc.setpoint, heater: doc.heater,
      shot: doc.state.map((s) => doc.states[s] === 'shot'),
    };
  }

  function App() {
    const [state, setState] = useState(null);
    const [history, setHistory] = useState(null);
    const [shots, setShots] = useState(null);
    const [params, setParams] = useState(null);
    const [lastSeen, setLastSeen] = useState(0);
    const [now, setNow] = useState(Date.now());

    const loadShots = useCallback(() => getJson('/api/shots').then(setShots).catch(() => {}), []);
    const loadParams = useCallback(() => getJson('/api/parameters').then(setParams).catch(() => {}), []);
    const loadState = useCallback(() => getJson('/api/state').then((s) => { setState(s); setLastSeen(Date.now()); })
      .catch(() => {}), []);

    useEffect(() => {
      loadState();
      getJson('/api/history').then((d) => setHistory(toSeries(d))).catch(() => {});
      loadShots();
      loadParams();

      const es = new EventSource('/api/events');
      es.addEventListener('state', (ev) => {
        const s = JSON.parse(ev.data);
        setState(s);
        setLastSeen(Date.now());
        setHistory((hst) => {
          if (!hst) return hst;
          const push = (arr, v) => { const a = arr.concat([v]); return a.length > HISTORY_LEN ? a.slice(-HISTORY_LEN) : a; };
          return {
            temperature: push(hst.temperature, s.temperature), setpoint: push(hst.setpoint, s.setpoint),
            heater: push(hst.heater, s.heater), shot: push(hst.shot, s.state === 'shot'),
          };
        });
      });
      for (const name of ['shot_finished', 'shot_complete']) es.addEventListener(name, loadShots);
      const tick = setInterval(() => setNow(Date.now()), 1000);
      return () => { es.close(); clearInterval(tick); };
    }, []);

    const stale = lastSeen > 0 ? now - lastSeen > STALE_MS : now - performance.timeOrigin > STALE_MS && !state;

    return h('div', null,
      h('div', { class: 'top' },
        h('h1', null, 'Silvia'),
        h('span', { class: 'conn' }, h('span', { class: 'dot' + (stale ? ' off' : '') }), stale ? 'offline' : 'live')),
      h(Banner, { state, stale }),
      h(Status, { state }),
      h(Controls, { state, onChanged: () => { loadState(); loadParams(); } }),
      h(History, { history }),
      h(Shots, { shots }),
      h(Expert, { params, onSaved: (p) => { setParams(p); loadState(); } }),
      state ? h('footer', null, state.version + ' · läuft seit ' + Math.round(state.uptime_s / 60) + ' min' +
        (state.wifi_rssi != null ? ' · WLAN ' + state.wifi_rssi + ' dBm' : '') +
        ' · MQTT ' + (state.mqtt_connected ? 'verbunden' : 'aus') +
        ' · ' + Math.round(state.free_heap / 1024) + ' KB frei') : null);
  }

  render(h(App), document.getElementById('app'));
})();
