async function fetchJSON(path, opts) {
  const res = await fetch(path, opts);
  if (!res.ok) {
    throw new Error(`${path} returned ${res.status}`);
  }
  return res.json();
}

document.querySelectorAll('.tabBtn').forEach((btn) => {
  btn.addEventListener('click', () => {
    document.querySelectorAll('.tabBtn').forEach((b) => b.classList.remove('active'));
    document.querySelectorAll('.tabContent').forEach((c) => c.classList.remove('active'));
    btn.classList.add('active');
    document.getElementById(`tab-${btn.dataset.tab}`).classList.add('active');
  });
});

async function loadHosts() {
  const hosts = await fetchJSON('/api/hosts');
  const list = document.getElementById('hostList');
  list.innerHTML = '';
  hosts.forEach(h => {
    const li = document.createElement('li');

    const label = document.createElement('span');
    label.textContent = `${h.name} (${h.host}) - ${h.enabled ? 'enabled' : 'disabled'} - ${h.reachable ? 'UP' : 'DOWN'} ${h.lastLatencyMs ? h.lastLatencyMs + 'ms' : ''}`;
    li.appendChild(label);

    const removeBtn = document.createElement('button');
    removeBtn.textContent = 'Remove';
    removeBtn.addEventListener('click', async () => {
      await fetch(`/api/hosts?id=${h.id}`, { method: 'DELETE' });
      loadHosts();
    });
    li.appendChild(removeBtn);

    list.appendChild(li);
  });
}

document.getElementById('addHostForm').addEventListener('submit', async (e) => {
  e.preventDefault();
  const name = document.getElementById('name').value;
  const host = document.getElementById('host').value;
  await fetch('/api/hosts', {method:'POST', headers:{'Content-Type':'application/json'}, body: JSON.stringify({name, host})});
  document.getElementById('name').value = '';
  document.getElementById('host').value = '';
  loadHosts();
});

function setBrightnessUI(percent) {
  document.getElementById('brightness').value = percent;
  document.getElementById('brightnessValue').textContent = `${percent}%`;
}

document.getElementById('brightness').addEventListener('input', (e) => {
  document.getElementById('brightnessValue').textContent = `${e.target.value}%`;
});

document.getElementById('saveSettings').addEventListener('click', async () => {
  const percent = parseInt(document.getElementById('brightness').value, 10);
  // Wire format / storage is still 0-255 (matches the OLED contrast API and
  // avoids a settings.json migration); the slider itself is percent-based
  // since that's what actually makes sense to a person setting brightness.
  const raw = Math.round((percent / 100) * 255);
  await fetch('/api/settings', {method:'POST', headers:{'Content-Type':'application/json'}, body: JSON.stringify({brightness: raw})});
});

document.getElementById('resetWifi').addEventListener('click', async () => {
  const confirmed = confirm(
    'This erases the device\'s saved WiFi credentials and opens the NETMON_SETUP portal. ' +
    'The device will NOT automatically reconnect to your current network -- ' +
    'you must complete setup via the portal or it stays disconnected. Continue?'
  );
  if (!confirmed) return;

  try {
    await fetch('/api/wifi/reset', {method: 'POST'});
    alert('NETMON_SETUP portal is opening. Connect to it from your phone/laptop, ' +
          'then browse to http://192.168.4.1:8080 to reconfigure WiFi ' +
          '(it may not auto-launch on this port) -- this page will stop responding once the device disconnects.');
  } catch (err) {
    console.error('Failed to request WiFi reset', err);
    alert('Failed to request WiFi reset -- device may already be unreachable.');
  }
});

// Page navigation -- permanent feature, not just a stand-in for the encoder.
async function callPageNav(endpoint) {
  try {
    const result = await fetch(endpoint, {method: 'POST'});
    const data = await result.json();
    document.getElementById('currentPageLabel').textContent = `Now showing: ${data.page}`;
  } catch (err) {
    console.error('Failed to switch page', err);
  }
}

document.getElementById('nextPage').addEventListener('click', () => callPageNav('/api/display/next-page'));
document.getElementById('prevPage').addEventListener('click', () => callPageNav('/api/display/prev-page'));

// --- Page config (name, display format, up to 4 InfluxDB-backed widgets) ---

// Page 2 starts pre-filled with an example Disk Usage query as a starting
// point -- purely a UI convenience, nothing is saved until "Save Widgets"
// is clicked. One query, three fields extracted from it (used/total/pct),
// so it only costs one InfluxDB round-trip instead of three.
const PAGE_DEFAULTS = {
  2: {
    name: 'Disk Usage',
    format: 'table',
    widgets: [
      {
        label: 'Used',
        query: 'SELECT last("raidTotalSize") - last("raidFreeSize") AS used_bytes, last("raidTotalSize") AS total_bytes, (last("raidTotalSize") - last("raidFreeSize")) / last("raidTotalSize") * 100 AS used_pct FROM "synology_volume" WHERE ("host" = \'homenas1\' AND "raidName" = \'Volume 1\')',
        field: 'used_bytes'
      },
      {
        label: 'Total',
        query: 'SELECT last("raidTotalSize") - last("raidFreeSize") AS used_bytes, last("raidTotalSize") AS total_bytes, (last("raidTotalSize") - last("raidFreeSize")) / last("raidTotalSize") * 100 AS used_pct FROM "synology_volume" WHERE ("host" = \'homenas1\' AND "raidName" = \'Volume 1\')',
        field: 'total_bytes'
      },
      {
        label: 'Used %',
        query: 'SELECT last("raidTotalSize") - last("raidFreeSize") AS used_bytes, last("raidTotalSize") AS total_bytes, (last("raidTotalSize") - last("raidFreeSize")) / last("raidTotalSize") * 100 AS used_pct FROM "synology_volume" WHERE ("host" = \'homenas1\' AND "raidName" = \'Volume 1\')',
        field: 'used_pct',
        max: '100'
      },
      {label: '', query: '', field: ''}
    ]
  }
};

function pageSectionHTML(n) {
  return `
    <h2>Page ${n}</h2>
    <div class="pageMetaRow">
      <label>Page Name<input id="pageName${n}" maxlength="16"></label>
      <label>Display Format
        <select id="pageFormat${n}">
          <option value="table">Table</option>
          <option value="barchart">Barchart</option>
        </select>
      </label>
      <button class="savePageMeta" data-page="${n}">Save</button>
    </div>

    <h3>Data Points</h3>
    <p class="hint">Up to 4 values from InfluxDB. Slots can share the same query (using "Field" to pick which column each one displays) to avoid querying the same thing multiple times, or each use their own independent query.</p>
    <div id="widgetGrid${n}"></div>
    <button class="saveWidgets" data-page="${n}">Save Widgets</button>
    <span class="hint" id="widgetSaveStatus${n}"></span>
  `;
}

function widgetRowHTML(n, slot) {
  return `
    <div class="widgetRow">
      <label>Label<input id="widgetLabel${n}_${slot}" maxlength="16" placeholder="e.g. Used"></label>
      <label>Query<textarea id="widgetQuery${n}_${slot}" rows="3" placeholder="SELECT last(value) FROM measurement WHERE ..."></textarea></label>
      <label>Field <span class="hint">(optional -- defaults to the first column)</span><input id="widgetField${n}_${slot}" placeholder="e.g. used_bytes"></label>
      <label>Max Value <span class="hint">(Barchart mode -- this widget's own 100% scale. Blank defaults to 100 for a "pct"/"percent" field, otherwise the bar just shows full for any positive value)</span><input id="widgetMax${n}_${slot}" placeholder="e.g. 100, or 2000000000000 for a 2TB volume in bytes"></label>
    </div>
  `;
}

async function loadPageSection(n) {
  const section = document.getElementById(`pageSection-${n}`);
  section.innerHTML = pageSectionHTML(n);
  document.getElementById(`widgetGrid${n}`).innerHTML = [0, 1, 2, 3].map((slot) => widgetRowHTML(n, slot)).join('');

  let cfg;
  try {
    cfg = await fetchJSON(`/api/page-config?page=${n}`);
  } catch (err) {
    console.error(`Failed to load config for page ${n}`, err);
    cfg = {name: `Page ${n}`, format: 'table', widgets: [{}, {}, {}, {}]};
  }

  const noWidgetsConfiguredYet = !cfg.widgets || cfg.widgets.every((w) => !w.label && !w.query);
  if (noWidgetsConfiguredYet && PAGE_DEFAULTS[n]) {
    // Nothing saved yet -- offer the example as a convenient starting point.
    cfg = PAGE_DEFAULTS[n];
  }

  document.getElementById(`pageName${n}`).value = cfg.name || `Page ${n}`;
  document.getElementById(`pageFormat${n}`).value = cfg.format || 'table';
  if (cfg.name) {
    document.querySelector(`.tabBtn[data-tab="page${n}"]`).textContent = cfg.name;
  }

  [0, 1, 2, 3].forEach((slot) => {
    const w = (cfg.widgets && cfg.widgets[slot]) || {};
    document.getElementById(`widgetLabel${n}_${slot}`).value = w.label || '';
    document.getElementById(`widgetQuery${n}_${slot}`).value = w.query || '';
    document.getElementById(`widgetField${n}_${slot}`).value = w.field || '';
    document.getElementById(`widgetMax${n}_${slot}`).value = w.max || '';
  });

  section.querySelector('.savePageMeta').addEventListener('click', async () => {
    const name = document.getElementById(`pageName${n}`).value.trim();
    const format = document.getElementById(`pageFormat${n}`).value;
    if (!name) return;
    try {
      await fetch('/api/page-meta', {
        method: 'POST',
        headers: {'Content-Type': 'application/json'},
        body: JSON.stringify({page: n, name, format})
      });
      document.querySelector(`.tabBtn[data-tab="page${n}"]`).textContent = name;
    } catch (err) {
      console.error(`Failed to save page ${n} meta`, err);
      alert('Failed to save page settings.');
    }
  });

  section.querySelector('.saveWidgets').addEventListener('click', async () => {
    const widgets = [0, 1, 2, 3].map((slot) => ({
      label: document.getElementById(`widgetLabel${n}_${slot}`).value.trim(),
      query: document.getElementById(`widgetQuery${n}_${slot}`).value.trim(),
      field: document.getElementById(`widgetField${n}_${slot}`).value.trim(),
      max: document.getElementById(`widgetMax${n}_${slot}`).value.trim()
    }));
    const status = document.getElementById(`widgetSaveStatus${n}`);
    try {
      await fetch('/api/page-widgets', {
        method: 'POST',
        headers: {'Content-Type': 'application/json'},
        body: JSON.stringify({page: n, widgets})
      });
      status.textContent = 'Saved -- refreshing data now.';
      setTimeout(() => { status.textContent = ''; }, 3000);
    } catch (err) {
      console.error(`Failed to save page ${n} widgets`, err);
      alert('Failed to save widgets.');
    }
  });
}

async function loadInflux() {
  try {
    const cfg = await fetchJSON('/api/influx');
    document.getElementById('influxHost').value = cfg.host || '';
    document.getElementById('influxPort').value = cfg.port || 8086;
    document.getElementById('influxDatabase').value = cfg.database || '';
    document.getElementById('influxUsername').value = cfg.username || '';
    document.getElementById('influxPassword').placeholder = cfg.passwordSet
      ? 'Currently set -- leave blank to keep'
      : 'Leave blank for no password';
  } catch (err) {
    console.error('Failed to load InfluxDB settings', err);
  }
}

document.getElementById('saveInflux').addEventListener('click', async () => {
  const host = document.getElementById('influxHost').value.trim();
  const port = parseInt(document.getElementById('influxPort').value, 10) || 8086;
  const database = document.getElementById('influxDatabase').value.trim();
  const username = document.getElementById('influxUsername').value.trim();
  const password = document.getElementById('influxPassword').value;

  const resultEl = document.getElementById('influxTestResult');
  const statusEl = document.getElementById('influxSaveStatus');
  const btn = document.getElementById('saveInflux');

  resultEl.className = '';
  resultEl.textContent = '';
  statusEl.textContent = '';
  btn.disabled = true;
  btn.textContent = 'Testing...';

  try {
    await fetch('/api/influx', {
      method: 'POST',
      headers: {'Content-Type': 'application/json'},
      body: JSON.stringify({host, port, database, username, password})
    });
    document.getElementById('influxPassword').value = '';

    const testRes = await fetch('/api/influx/test', {method: 'POST'});
    const result = await testRes.json();

    const success = result.reachable && result.authOk && (database === '' || result.databaseFound);
    resultEl.className = success ? 'influxResult success' : 'influxResult error';
    resultEl.textContent = result.message;

    loadInflux();
  } catch (err) {
    console.error('Failed to save/test InfluxDB settings', err);
    resultEl.className = 'influxResult error';
    resultEl.textContent = 'Request failed -- device may be unreachable.';
  } finally {
    btn.disabled = false;
    btn.textContent = 'Save & Test';
  }
});

// Default of 100% (max contrast) is used whenever the current brightness
// can't be determined -- request failure, malformed response, or a
// missing/non-numeric brightness field.
const DEFAULT_BRIGHTNESS_PERCENT = 100;

async function loadSettings() {
  try {
    const s = await fetchJSON('/api/settings');
    const raw = (s && typeof s.brightness === 'number') ? s.brightness : 255;
    const percent = Math.round((raw / 255) * 100);
    setBrightnessUI(percent);
  } catch (err) {
    console.error('Failed to load settings, defaulting to max contrast', err);
    setBrightnessUI(DEFAULT_BRIGHTNESS_PERCENT);
  }
}

async function loadLogs() {
  const paused = document.getElementById('logPaused').checked;
  if (paused) return;

  try {
    const lines = await fetchJSON('/api/logs');
    const view = document.getElementById('logView');
    view.textContent = lines.join('\n');

    if (document.getElementById('logAutoscroll').checked) {
      view.scrollTop = view.scrollHeight;
    }
  } catch (err) {
    console.error('Failed to load logs', err);
  }
}

window.addEventListener('load', () => {
  loadHosts();
  loadSettings();
  loadLogs();
  loadInflux();
  [2, 3, 4, 5].forEach(loadPageSection);
  setInterval(loadHosts, 3000);
  setInterval(loadLogs, 2000);
});