async function fetchJSON(path, opts) {
  const res = await fetch(path, opts);
  if (!res.ok) {
    throw new Error(`${path} returned ${res.status}`);
  }
  return res.json();
}

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
  setInterval(loadHosts, 3000);
  setInterval(loadLogs, 2000);
});