// pages.h
// Web UI served straight from flash (PROGMEM), so no separate LittleFS data upload
// is required for the pages themselves - only /config.json lives on LittleFS.

#pragma once

// ---------------------------------------------------------------------------
// Dashboard: live weight as text + circular dial gauge, updated over WebSocket
// ---------------------------------------------------------------------------
const char INDEX_HTML[] PROGMEM = R"HTMLPAGE(
<!DOCTYPE html>
<html>
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>Scale</title>
<style>
  :root { --bg:#0f1420; --card:#182033; --fg:#e8ecf4; --accent:#4fd1c5; --muted:#7c8aa5; }
  * { box-sizing:border-box; }
  body { margin:0; font-family:-apple-system,Segoe UI,Roboto,sans-serif; background:var(--bg); color:var(--fg); }
  header { display:flex; justify-content:space-between; align-items:center; padding:14px 20px; border-bottom:1px solid #24304a; }
  header a { color:var(--muted); text-decoration:none; font-size:14px; }
  header a:hover { color:var(--accent); }
  .wrap { max-width:480px; margin:0 auto; padding:24px 20px 40px; text-align:center; }
  .card { background:var(--card); border-radius:16px; padding:24px; }
  #weight { font-size:56px; font-weight:700; margin:6px 0 0; }
  #unit { font-size:18px; color:var(--muted); }
  #status { font-size:13px; color:var(--muted); margin-top:6px; }
  #status.ok::before { content:"● "; color:#3ecf6a; }
  #status.bad::before { content:"● "; color:#e5534b; }
  button { background:var(--accent); border:none; color:#08201d; font-weight:600; padding:12px 22px; border-radius:10px; font-size:15px; margin-top:20px; cursor:pointer; }
  button:active { transform:scale(0.98); }
  svg { max-width:100%; }
</style>
</head>
<body>
<header>
  <strong id="hostLabel">Scale</strong>
  <a href="/setup">Setup &amp; Calibration</a>
</header>
<div class="wrap">
  <div class="card">
    <svg viewBox="0 0 240 150" width="280" height="175">
      <path d="M 20 130 A 100 100 0 1 1 220 130" fill="none" stroke="#24304a" stroke-width="16" stroke-linecap="round"/>
      <path id="arcFill" d="M 20 130 A 100 100 0 1 1 220 130" fill="none" stroke="#4fd1c5" stroke-width="16" stroke-linecap="round"
            stroke-dasharray="330" stroke-dashoffset="330"/>
      <g id="needle" style="transform-origin:120px 130px;">
        <line x1="120" y1="130" x2="120" y2="45" stroke="#e8ecf4" stroke-width="4" stroke-linecap="round"/>
        <circle cx="120" cy="130" r="7" fill="#e8ecf4"/>
      </g>
      <text x="20" y="148" fill="#7c8aa5" font-size="11">0</text>
      <text id="maxLabel" x="205" y="148" fill="#7c8aa5" font-size="11" text-anchor="end">max</text>
    </svg>
    <div id="weight">--</div>
    <div id="unit">unit</div>
    <div id="status" class="bad">connecting...</div>
    <button id="tareBtn">Tare / Zero</button>
  </div>
</div>
<script>
let maxWeight = 100, decimals = 1, unit = "kg";
const weightEl = document.getElementById('weight');
const unitEl = document.getElementById('unit');
const statusEl = document.getElementById('status');
const needle = document.getElementById('needle');
const arcFill = document.getElementById('arcFill');
const maxLabel = document.getElementById('maxLabel');

function applyReading(w) {
  weightEl.textContent = Number(w).toFixed(decimals);
  let pct = Math.max(0, Math.min(1, w / maxWeight));
  // Gauge sweeps 270 degrees, needle starts pointing at -135deg (zero) up to +135deg (max)
  let deg = -135 + pct * 270;
  needle.style.transform = 'rotate(' + deg + 'deg)';
  let circumference = 330; // approx path length used for stroke-dasharray above
  arcFill.setAttribute('stroke-dashoffset', circumference - pct * circumference);
}

function setStatus(ok, text) {
  statusEl.textContent = text;
  statusEl.className = ok ? 'ok' : 'bad';
}

let ws;
let pollTimer = null;

function startPolling() {
  if (pollTimer) return;
  pollTimer = setInterval(() => {
    fetch('/api/weight').then(r => r.json()).then(d => {
      setStatus(true, 'polling');
      applyReading(d.weight);
    }).catch(() => setStatus(false, 'offline'));
  }, 500);
}

function connectWS() {
  ws = new WebSocket('ws://' + location.host + '/ws');
  ws.onopen = () => setStatus(true, 'live');
  ws.onmessage = (evt) => {
    try {
      const d = JSON.parse(evt.data);
      if (d.max_weight) { maxWeight = d.max_weight; }
      if (d.unit) { unit = d.unit; unitEl.textContent = unit; maxLabel.textContent = maxWeight + ' ' + unit; }
      if (typeof d.decimals === 'number') decimals = d.decimals;
      if (typeof d.weight === 'number') applyReading(d.weight);
    } catch (e) {}
  };
  ws.onclose = () => { setStatus(false, 'reconnecting...'); setTimeout(connectWS, 2000); startPolling(); };
  ws.onerror = () => { ws.close(); };
}

document.getElementById('tareBtn').onclick = () => {
  fetch('/api/tare', { method: 'POST' }).then(() => setStatus(true, 'tared'));
};

fetch('/api/config').then(r => r.json()).then(c => {
  document.getElementById('hostLabel').textContent = c.hostname || 'Scale';
  maxWeight = c.max_weight || maxWeight;
  unit = c.unit || unit;
  decimals = (typeof c.decimals === 'number') ? c.decimals : decimals;
  unitEl.textContent = unit;
  maxLabel.textContent = maxWeight + ' ' + unit;
});

connectWS();
</script>
</body>
</html>
)HTMLPAGE";

// ---------------------------------------------------------------------------
// Setup page: Wi-Fi, AP, hostname, display + calibration wizard
// ---------------------------------------------------------------------------
const char SETUP_HTML[] PROGMEM = R"HTMLPAGE(
<!DOCTYPE html>
<html>
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>Scale Setup</title>
<style>
  :root { --bg:#0f1420; --card:#182033; --fg:#e8ecf4; --accent:#4fd1c5; --muted:#7c8aa5; --danger:#e5534b; }
  * { box-sizing:border-box; }
  body { margin:0; font-family:-apple-system,Segoe UI,Roboto,sans-serif; background:var(--bg); color:var(--fg); }
  header { display:flex; justify-content:space-between; align-items:center; padding:14px 20px; border-bottom:1px solid #24304a; }
  header a { color:var(--muted); text-decoration:none; font-size:14px; }
  header a:hover { color:var(--accent); }
  .wrap { max-width:520px; margin:0 auto; padding:20px 20px 60px; }
  .card { background:var(--card); border-radius:16px; padding:20px; margin-bottom:18px; }
  h3 { margin:0 0 14px; font-size:15px; color:var(--accent); text-transform:uppercase; letter-spacing:.04em; }
  label { display:block; font-size:13px; color:var(--muted); margin:12px 0 4px; }
  input, select { width:100%; padding:10px 12px; border-radius:8px; border:1px solid #2c3a5a; background:#0f1420; color:var(--fg); font-size:14px; }
  .row { display:flex; gap:10px; }
  .row > div { flex:1; }
  button { background:var(--accent); border:none; color:#08201d; font-weight:600; padding:11px 18px; border-radius:9px; font-size:14px; margin-top:14px; cursor:pointer; }
  button.secondary { background:#2c3a5a; color:var(--fg); }
  button.danger { background:var(--danger); color:#fff; }
  #toast { position:fixed; bottom:18px; left:50%; transform:translateX(-50%); background:#182033; border:1px solid var(--accent); padding:10px 18px; border-radius:8px; font-size:14px; display:none; }
  #rawVal { font-size:13px; color:var(--muted); margin-top:8px; }
  small.hint { color:var(--muted); display:block; margin-top:4px; }
</style>
</head>
<body>
<header>
  <strong>Scale Setup</strong>
  <a href="/">&larr; Dashboard</a>
</header>
<div class="wrap">

  <div class="card">
    <h3>Home Wi-Fi (Station mode)</h3>
    <label>Network (SSID)</label>
    <div class="row">
      <div><input id="wifi_ssid" list="ssidList" placeholder="SSID"></div>
      <div style="flex:0 0 auto;"><button class="secondary" type="button" id="scanBtn">Scan</button></div>
    </div>
    <datalist id="ssidList"></datalist>
    <label>Password</label>
    <input id="wifi_pass" type="password" placeholder="Wi-Fi password">
    <small class="hint">Leave the button on the board unpressed at power-up to auto-connect here within the timeout below.</small>
  </div>

  <div class="card">
    <h3>Access Point (fallback / configuration)</h3>
    <label>AP SSID</label>
    <input id="ap_ssid" placeholder="ScaleSetup">
    <label>AP Password (min 8 chars, blank = open)</label>
    <input id="ap_pass" type="password" placeholder="12345678">
    <small class="hint">The AP stays available so you can always reach this page, even after connecting to home Wi-Fi.</small>
  </div>

  <div class="card">
    <h3>Network Identity</h3>
    <label>Hostname (device will answer at http://&lt;hostname&gt;.local)</label>
    <input id="hostname" placeholder="scale1">
    <label>Station connect timeout (ms)</label>
    <input id="sta_timeout_ms" type="number" min="5000" step="1000">
  </div>

  <div class="card">
    <h3>Display</h3>
    <div class="row">
      <div><label>Unit label</label><input id="unit" placeholder="kg"></div>
      <div><label>Decimals</label><input id="decimals" type="number" min="0" max="3"></div>
    </div>
    <label>Gauge full-scale (max weight)</label>
    <input id="max_weight" type="number" step="0.1">
  </div>

  <div class="card">
    <h3>Calibration</h3>
    <div id="rawVal">raw: -- &nbsp;|&nbsp; weight: --</div>
    <label>Step 1 &mdash; remove all weight, then zero the scale</label>
    <button type="button" id="tareBtn">Tare / Zero (saved)</button>
    <label>Step 2 &mdash; place a known reference weight, enter its value, then calibrate</label>
    <input id="knownWeight" type="number" step="0.01" placeholder="e.g. 20.0">
    <button type="button" id="calBtn">Calibrate</button>
  </div>

  <button id="saveBtn">Save Configuration</button>
  <button class="danger" id="rebootBtn" type="button">Save &amp; Reboot</button>
</div>

<div id="toast"></div>

<script>
function toast(msg) {
  const t = document.getElementById('toast');
  t.textContent = msg;
  t.style.display = 'block';
  setTimeout(() => t.style.display = 'none', 2500);
}

function loadConfig() {
  fetch('/api/config').then(r => r.json()).then(c => {
    for (const k of ['wifi_ssid','ap_ssid','ap_pass','hostname','unit']) {
      if (document.getElementById(k)) document.getElementById(k).value = c[k] ?? '';
    }
    document.getElementById('max_weight').value = c.max_weight ?? 100;
    document.getElementById('decimals').value = c.decimals ?? 1;
    document.getElementById('sta_timeout_ms').value = c.sta_timeout_ms ?? 60000;
  });
}

function pollRaw() {
  fetch('/api/weight').then(r => r.json()).then(d => {
    document.getElementById('rawVal').textContent = 'raw: ' + d.raw + '  |  weight: ' + Number(d.weight).toFixed(3);
  }).catch(() => {});
}
setInterval(pollRaw, 700);

document.getElementById('scanBtn').onclick = () => {
  toast('Scanning...');
  fetch('/api/scan').then(r => r.json()).then(list => {
    const dl = document.getElementById('ssidList');
    dl.innerHTML = '';
    list.forEach(n => {
      const opt = document.createElement('option');
      opt.value = n.ssid;
      dl.appendChild(opt);
    });
    toast('Found ' + list.length + ' networks');
  });
};

document.getElementById('tareBtn').onclick = () => {
  fetch('/api/tare', { method: 'POST' }).then(() => toast('Zero point saved'));
};

document.getElementById('calBtn').onclick = () => {
  const kw = parseFloat(document.getElementById('knownWeight').value);
  if (!kw || kw <= 0) { toast('Enter a valid known weight first'); return; }
  fetch('/api/calibrate', {
    method: 'POST',
    headers: { 'Content-Type': 'application/json' },
    body: JSON.stringify({ known_weight: kw })
  }).then(r => r.json()).then(d => {
    toast(d.ok ? 'Calibrated. Factor saved.' : 'Calibration failed');
  });
};

function gatherConfig() {
  return {
    wifi_ssid: document.getElementById('wifi_ssid').value,
    wifi_pass: document.getElementById('wifi_pass').value,
    ap_ssid: document.getElementById('ap_ssid').value,
    ap_pass: document.getElementById('ap_pass').value,
    hostname: document.getElementById('hostname').value,
    unit: document.getElementById('unit').value,
    decimals: parseInt(document.getElementById('decimals').value || '1', 10),
    max_weight: parseFloat(document.getElementById('max_weight').value || '100'),
    sta_timeout_ms: parseInt(document.getElementById('sta_timeout_ms').value || '60000', 10)
  };
}

document.getElementById('saveBtn').onclick = () => {
  fetch('/api/config', {
    method: 'POST',
    headers: { 'Content-Type': 'application/json' },
    body: JSON.stringify(gatherConfig())
  }).then(() => toast('Saved. Reboot to apply Wi-Fi/hostname changes.'));
};

document.getElementById('rebootBtn').onclick = () => {
  fetch('/api/config', {
    method: 'POST',
    headers: { 'Content-Type': 'application/json' },
    body: JSON.stringify(gatherConfig())
  }).then(() => fetch('/api/reboot', { method: 'POST' }))
    .then(() => toast('Rebooting...'));
};

loadConfig();
</script>
</body>
</html>
)HTMLPAGE";
