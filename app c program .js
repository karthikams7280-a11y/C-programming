const $ = s => document.querySelector(s);
let S = null;

function show(v) {
  document.querySelectorAll('.view').forEach(e => e.hidden = e.id !== v);
  document.querySelectorAll('nav a').forEach(a => a.classList.toggle('on', a.dataset.v === v));
  $('#title').textContent = document.querySelector(`nav a[data-v="${v}"]`).textContent;
}
document.querySelectorAll('nav a').forEach(a => a.onclick = () => show(a.dataset.v));

async function load() { S = await (await fetch('/api/status')).json(); render(); }
async function post(url, data) {
  const r = await fetch(url, { method: 'POST', body: new URLSearchParams(data) });
  return r.json();
}

function render() {
  const pct = Math.round(S.parked / S.total * 100);
  const card = (l, v) => `<div class="stat"><span>${l}</span><b>${v}</b></div>`;
  $('#stats').innerHTML = card('Total slots', S.total) + card('Available slots', S.available) +
    card('Vehicles parked', S.parked) + card('Vehicles exited', S.exited) +
    card('Parking revenue', '₹' + S.revenue) + card('Occupancy rate', pct + '%');
  $('#fill').style.width = pct + '%';
  const grid = S.slots.map(s => `<div class="slot ${s.plate ? 'busy' : 'free'}" data-p="${s.plate}">
    <b>${s.n}</b><small>${s.plate ? s.plate + ' (' + s.type + ')' : 'Free'}</small></div>`).join('');
  $('#grid1').innerHTML = $('#grid2').innerHTML = grid;
  document.querySelectorAll('.slot.busy').forEach(el => el.onclick = () => { $('#xplate').value = el.dataset.p; show('exit'); });
  $('#rows').innerHTML = S.records.length ? S.records.map(r => `<tr><td>${r.id}</td><td>${r.plate}</td><td>${r.type}</td>
    <td>${r.slot}</td><td>${r.in}</td><td>${r.out === '-' ? 'Parked' : r.out}</td>
    <td>${r.out === '-' ? '-' : '₹' + r.amount}</td></tr>`).join('')
    : '<tr><td colspan="7">No records yet. Register a vehicle to begin.</td></tr>';
}

$('#fEntry').onsubmit = async e => {
  e.preventDefault();
  const r = await post('/api/entry', { plate: $('#eplate').value, type: $('#etype').value });
  $('#eres').innerHTML = r.ok
    ? `<div class="msg ok">${r.plate} is allocated to slot ${r.slot}.</div>`
    : `<div class="msg bad">${r.msg}</div>`;
  if (r.ok) $('#eplate').value = '';
  load();
};

$('#fExit').onsubmit = async e => {
  e.preventDefault();
  const r = await post('/api/exit', { plate: $('#xplate').value });
  $('#xres').innerHTML = r.ok ? `<div class="bill">
    <div><span>Vehicle</span><b>${r.plate} (${r.type})</b></div><div><span>Slot</span><b>${r.slot}</b></div>
    <div><span>Entry</span><span>${r.in}</span></div><div><span>Exit</span><span>${r.out}</span></div>
    <div><span>Duration</span><span>${r.mins} min, billed ${r.hours} h</span></div>
    <div><span>Rate</span><span>₹${r.rate} per hour</span></div>
    <div class="tot"><span>Total</span><span>₹${r.amount}</span></div></div>`
    : `<div class="msg bad">${r.msg}</div>`;
  if (r.ok) $('#xplate').value = '';
  load();
};

$('#reset').onclick = async () => { if (confirm('Delete all parking records?')) { await post('/api/reset', {}); load(); } };

setInterval(() => $('#clock').textContent = new Date().toLocaleString('en-IN', { dateStyle: 'medium', timeStyle: 'short' }), 1000);
load();
