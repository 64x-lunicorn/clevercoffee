/**
 * @file StatusPage.h
 * @brief Small offline status page served at "/" until the full web UI arrives (step 3).
 */
#pragma once

#include <Arduino.h>

static const char statusPage[] PROGMEM = R"HTML(<!doctype html>
<html lang="de"><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>Silvia</title>
<style>
:root{color-scheme:light dark;--bg:#fff;--fg:#111;--muted:#666;--card:#f3f3f3;--accent:#b5482a}
@media (prefers-color-scheme:dark){:root{--bg:#121212;--fg:#eee;--muted:#999;--card:#1e1e1e;--accent:#e07a5f}}
body{margin:0;padding:16px;font:16px system-ui,sans-serif;background:var(--bg);color:var(--fg);max-width:480px}
.card{background:var(--card);border-radius:12px;padding:16px;margin-bottom:12px}
.temp{font-size:56px;font-weight:600}.muted{color:var(--muted)}.err{color:#d33;font-weight:600}
button{font-size:18px;padding:10px 16px;border-radius:8px;border:0;background:var(--accent);color:#fff}
.row{display:flex;gap:8px;align-items:center}
</style></head><body>
<div class="card"><div class="temp" id="t">–</div><div id="s" class="muted">verbinde…</div>
<div id="shot"></div><div id="e" class="err"></div></div>
<div class="card"><div class="muted">Brühtemperatur</div>
<div class="row"><button onclick="sp(-0.5)">−</button><b id="sp">–</b><button onclick="sp(0.5)">+</button></div></div>
<div class="card row"><button id="steam" onclick="steam()">Dampf</button><span id="msg" class="muted"></span></div>
<div class="muted">API: <a href="/api/state">/api/state</a> · <a href="/api/parameters">/api/parameters</a></div>
<script>
let st=null;const $=i=>document.getElementById(i);
const ERR={sensor:'Fühler-Fehler – Heizung aus. Fühler/Kabel prüfen, Maschine aus- und einschalten.',overtemp:'Zu heiß – Heizung aus, bis die Temperatur gefallen ist.'};
async function poll(){try{const r=await fetch('/api/state');st=await r.json();
$('t').textContent=st.temperature==null?'–':st.temperature.toFixed(1)+' °C';
$('s').textContent=st.state_label+(st.ready?' ✓':'');$('sp').textContent=st.brew_setpoint.toFixed(1)+' °C';
$('shot').textContent=st.shot_timer_s!=null?'Shot: '+st.shot_timer_s.toFixed(0)+' s':(st.last_shot?'Letzter Shot: '+st.last_shot.duration_s.toFixed(1)+' s':'');
$('e').textContent=st.error!='none'?ERR[st.error]:'';$('steam').textContent=st.steam_on?'Dampf aus':'Dampf an';
}catch(e){$('s').textContent='Keine Verbindung zur Maschine';}}
async function post(url,body){const r=await fetch(url,{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify(body)});
$('msg').textContent=r.ok?'gespeichert':(r.status==401?'Passwort nötig':'abgelehnt');poll();}
function sp(d){if(st)post('/api/parameters',{brew_setpoint:Math.round((st.brew_setpoint+d)*10)/10});}
function steam(){if(st)post('/api/command',{steam:!st.steam_on});}
poll();setInterval(poll,2000);
</script></body></html>)HTML";
