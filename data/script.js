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

async function loadPageNames() {
  try {
    const names = await fetchJSON('/api/page-names');
    document.getElementById('pageName2').value = names.page2 || '';
    document.getElementById('pageName3').value = names.page3 || '';
    document.getElementById('pageName4').value = names.page4 || '';
    document.getElementById('pageName5').value = names.page5 || '';

    // Keep the tab labels themselves in sync with saved names too, not
    // just the input fields inside each tab.
    [2, 3, 4, 5].forEach((n) => {
      const name = names[`page${n}`];
      if (name) {
        document.querySelector(`.tabBtn[data-tab="page${n}"]`).textContent = name;
      }
    });
  } catch (err) {
    console.error('Failed to load page names', err);
  }
}

document.querySelectorAll('.savePageName').forEach((btn) => {
  btn.addEventListener('click', async () => {
    const page = parseInt(btn.dataset.page, 10);
    const name = document.getElementById(`pageName${page}`).value.trim();
    if (!name) return;
    try {
      await fetch('/api/page-name', {
        method: 'POST',
        headers: {'Content-Type': 'application/json'},
        body: JSON.stringify({page, name})
      });
      // Also update this tab's own button label so it stays in sync
      // without needing a page reload.
      document.querySelector(`.tabBtn[data-tab="page${page}"]`).textContent = name;
    } catch (err) {
      console.error('Failed to save page name', err);
      alert('Failed to save page name.');
    }
  });
});

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
  loadPageNames();
  loadInflux();
  setInterval(loadHosts, 3000);
  setInterval(loadLogs, 2000);
});