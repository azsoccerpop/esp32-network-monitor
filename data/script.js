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

function setBrightnessUI(value) {
  document.getElementById('brightness').value = value;
  document.getElementById('brightnessValue').textContent = value;
}

document.getElementById('brightness').addEventListener('input', (e) => {
  document.getElementById('brightnessValue').textContent = e.target.value;
});

document.getElementById('saveSettings').addEventListener('click', async () => {
  const b = parseInt(document.getElementById('brightness').value, 10);
  await fetch('/api/settings', {method:'POST', headers:{'Content-Type':'application/json'}, body: JSON.stringify({brightness: b})});
});

// Default of 255 (max contrast) is used whenever the current brightness
// can't be determined -- request failure, malformed response, or a
// missing/non-numeric brightness field.
const DEFAULT_BRIGHTNESS = 255;

async function loadSettings() {
  try {
    const s = await fetchJSON('/api/settings');
    const value = (s && typeof s.brightness === 'number') ? s.brightness : DEFAULT_BRIGHTNESS;
    setBrightnessUI(value);
  } catch (err) {
    console.error('Failed to load settings, defaulting to max contrast', err);
    setBrightnessUI(DEFAULT_BRIGHTNESS);
  }
}

window.addEventListener('load', () => {
  loadHosts();
  loadSettings();
  setInterval(loadHosts, 3000);
});