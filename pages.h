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
  .dial-wrap { margin-bottom:16px; }
  .tick { stroke:#3a4763; stroke-width:2; }
  .tick-label { fill:#7c8aa5; font-size:9px; }
</style>
</head>
<body>
<header>
  <strong id="hostLabel">Scale</strong>
  <a href="/setup">Setup &amp; Calibration</a>
</header>
<div class="wrap">
  <div class="card">
    <div class="dial-wrap">
    <svg id="dialSvg" viewBox="0 0 240 216" width="280" height="252">
      <!-- Arc endpoints (49.3,200.7) and (190.7,200.7) are the needle's own
           0%/100% positions (radius 100, angles -135deg/+135deg about the
           120,130 pivot) - the arc spans exactly the needle's sweep range,
           so the fill boundary and the needle always point at the same spot. -->
      <path d="M 49.3 200.7 A 100 100 0 1 1 190.7 200.7" fill="none" stroke="#24304a" stroke-width="16" stroke-linecap="round"/>
      <path id="arcFill" d="M 49.3 200.7 A 100 100 0 1 1 190.7 200.7" fill="none" stroke="#4fd1c5" stroke-width="16" stroke-linecap="round"
            stroke-dasharray="471.24" stroke-dashoffset="471.24"/>
      <g id="ticks"></g>
      <g id="needle" style="transform-origin:120px 130px;">
        <line x1="120" y1="130" x2="120" y2="45" stroke="#e8ecf4" stroke-width="4" stroke-linecap="round"/>
        <circle cx="120" cy="130" r="7" fill="#e8ecf4"/>
      </g>
      <text x="70" y="186" fill="#7c8aa5" font-size="11" text-anchor="middle">0</text>
      <text id="maxLabel" x="170" y="186" fill="#7c8aa5" font-size="11" text-anchor="middle">max</text>
    </svg>
    </div>
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
const ticksGroup = document.getElementById('ticks');
const dialSvg = document.getElementById('dialSvg');

// Watermark logo (centered in the dial). Paste a base64 data URI here
// (e.g. "data:image/png;base64,...") once the Recyclemania logo asset is
// available; left blank the watermark is simply omitted.
const LOGO_DATA_URI = 'data:image/png;base64,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';

function renderLogoWatermark() {
  if (!LOGO_DATA_URI) return;
  const size = 56;
  const img = document.createElementNS('http://www.w3.org/2000/svg', 'image');
  img.setAttribute('href', LOGO_DATA_URI);
  img.setAttribute('x', 120 - size / 2);
  img.setAttribute('y', 108 - size / 2);
  img.setAttribute('width', size);
  img.setAttribute('height', size);
  img.setAttribute('opacity', '0.15');
  img.setAttribute('preserveAspectRatio', 'xMidYMid meet');
  // Insert behind the ticks/needle so it reads as a watermark, not an overlay.
  dialSvg.insertBefore(img, ticksGroup);
}

// Tick marks around the dial, spaced evenly from 0 to max_weight. Regenerated
// whenever max_weight changes (initial config load, or a live config update),
// so the graduations always match the currently configured full-scale value.
function renderTicks(max) {
  const cx = 120, cy = 130;
  const outerR = 100, innerR = 86, labelR = 70;
  const tickCount = 5; // 4 intermediate marks: 20%, 40%, 60%, 80%
  ticksGroup.innerHTML = '';
  // Skip i=0 and i=tickCount (0% and 100%): at those angles the tick and its
  // label fall below the needle pivot (same "dips below the horizontal" spot
  // the needle itself swings through at the extremes), landing off away from
  // the arc instead of next to it. The existing static "0"/max labels already
  // mark those two endpoints in sensible positions, so no marks are lost.
  for (let i = 1; i < tickCount; i++) {
    const frac = i / tickCount;
    const rad = (-135 + frac * 270) * Math.PI / 180;
    const sin = Math.sin(rad), cos = Math.cos(rad);

    const line = document.createElementNS('http://www.w3.org/2000/svg', 'line');
    line.setAttribute('x1', cx + innerR * sin);
    line.setAttribute('y1', cy - innerR * cos);
    line.setAttribute('x2', cx + outerR * sin);
    line.setAttribute('y2', cy - outerR * cos);
    line.setAttribute('class', 'tick');
    ticksGroup.appendChild(line);

    const text = document.createElementNS('http://www.w3.org/2000/svg', 'text');
    text.setAttribute('x', cx + labelR * sin);
    text.setAttribute('y', cy - labelR * cos + 3);
    text.setAttribute('text-anchor', 'middle');
    text.setAttribute('class', 'tick-label');
    text.textContent = (max * frac).toFixed(decimals);
    ticksGroup.appendChild(text);
  }
}

// Smoothed (EMA) weight used for both the numeric readout and the needle/arc,
// so small HX711 sample-to-sample noise doesn't make the display jump around.
// Lower SMOOTHING_ALPHA = smoother but laggier; higher = snappier but jumpier.
const SMOOTHING_ALPHA = 0.3;
let displayedWeight = null;

function applyReading(w) {
  if (!Number.isFinite(w)) return; // ignore NaN (e.g. HX711 not ready) rather than poisoning the average
  displayedWeight = (displayedWeight === null) ? w : displayedWeight + SMOOTHING_ALPHA * (w - displayedWeight);
  weightEl.textContent = displayedWeight.toFixed(decimals);
  let pct = Math.max(0, Math.min(1, displayedWeight / maxWeight));
  // Gauge sweeps 270 degrees, needle starts pointing at -135deg (zero) up to +135deg (max)
  let deg = -135 + pct * 270;
  needle.style.transform = 'rotate(' + deg + 'deg)';
  // Arc path is a true 270deg sweep at radius 100: length = 100 * (270*PI/180).
  // Matches the needle's angle exactly, so the fill always ends at the needle tip.
  let circumference = 471.24;
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
      if (d.max_weight && d.max_weight !== maxWeight) { maxWeight = d.max_weight; renderTicks(maxWeight); }
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

renderTicks(maxWeight);
renderLogoWatermark();

fetch('/api/config').then(r => r.json()).then(c => {
  document.getElementById('hostLabel').textContent = c.hostname || 'Scale';
  maxWeight = c.max_weight || maxWeight;
  unit = c.unit || unit;
  decimals = (typeof c.decimals === 'number') ? c.decimals : decimals;
  unitEl.textContent = unit;
  maxLabel.textContent = maxWeight + ' ' + unit;
  renderTicks(maxWeight);
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
