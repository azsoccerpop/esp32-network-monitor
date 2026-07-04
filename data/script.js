async function fetchJSON(path, opts) {
  const res = await fetch(path, opts);
  return res.json();
}

async function loadHosts() {
  const hosts = await fetchJSON('/api/hosts');
  const list = document.getElementById('hostList');
  list.innerHTML = '';
  hosts.forEach(h => {
    const li = document.createElement('li');
    li.textContent = `${h.name} (${h.host}) - ${h.enabled ? 'enabled' : 'disabled'} - ${h.reachable ? 'UP' : 'DOWN'} ${h.lastLatencyMs ? h.lastLatencyMs + 'ms' : ''}`;
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

document.getElementById('saveSettings').addEventListener('click', async () => {
  const b = parseInt(document.getElementById('brightness').value, 10);
  await fetch('/api/settings', {method:'POST', headers:{'Content-Type':'application/json'}, body: JSON.stringify({brightness: b})});
});

async function loadSettings() {
  const s = await fetchJSON('/api/settings');
  document.getElementById('brightness').value = s.brightness || 128;
}

window.addEventListener('load', () => {
  loadHosts();
  loadSettings();
  setInterval(loadHosts, 3000);
});
