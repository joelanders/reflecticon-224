// The page: pick a machine, bring its ROMs (kept in this browser's IndexedDB), play a source
// through it. The machine runs in an AudioWorklet (worklet-<id>.mjs); a second instance on this
// thread reads the parameter table and recognises ROM files.
import { Machine } from './machine.mjs';
import { makeSource } from './sounds.mjs';
import { Panel, panelDefs } from './panel.mjs';

const MACHINES = [
  { id: 'dn780', label: 'DN780' },
  { id: 'rmx16', label: 'RMX16' },
  { id: 'dmx1580', label: 'DMX 15-80' },
  { id: 'sg323', label: 'StarGate 323' },
];

const $ = (id) => document.getElementById(id);
let id = MACHINES.find((m) => '#' + m.id === location.hash)?.id || localStorage.getItem('kit-web:last') || 'sg323';
let machine = null;      // main-thread instance (no ROMs booted)
let files = [];          // [{name, bytes}] that fit some role
let rec = null;          // recognise() result
let values = [];         // plain parameter values, table order
let ctx = null, node = null, src = null, srcNode = null, micStream = null, fileBuffer = null;
let dryGain, wetGain, master, sourceBus, analyser;
let sendQueued = false;
let layout = null, panel = null, inputs = {}, adoptSerial = -1;   // hardware panel

// ---------- ROM store (IndexedDB: one record per machine, the files as ArrayBuffers) ----------
const db = new Promise((res, rej) => {
  const r = indexedDB.open('kit-web-roms', 1);
  r.onupgradeneeded = () => r.result.createObjectStore('files');
  r.onsuccess = () => res(r.result);
  r.onerror = () => rej(r.error);
});
async function idb(mode, fn) {
  const d = await db;
  return new Promise((res, rej) => {
    const t = d.transaction('files', mode);
    const q = fn(t.objectStore('files'));
    t.oncomplete = () => res(q?.result);
    t.onerror = () => rej(t.error);
  });
}
const loadFiles = async (mid) => ((await idb('readonly', (s) => s.get(mid)).catch(() => null)) || [])
  .map((f) => ({ name: f.name, bytes: new Uint8Array(f.bytes) }));
const saveFiles = (mid, fs) => idb('readwrite', (s) => s.put(fs.map((f) => ({ name: f.name, bytes: f.bytes.buffer.slice(0) })), mid));

// ---------- parameters ----------
const PKEY = () => `kit-web:${id}:params`;
function loadValues() {
  let saved = {};
  try { saved = JSON.parse(localStorage.getItem(PKEY()) || '{}'); } catch {}
  return machine.info.params.map((p) => (p.id in saved ? saved[p.id] : p.def));
}
function saveValues() {
  try { localStorage.setItem(PKEY(), JSON.stringify(Object.fromEntries(machine.info.params.map((p, i) => [p.id, values[i]])))); } catch {}
}
function sendValues() {
  saveValues();
  if (!node || sendQueued) return;
  sendQueued = true;
  setTimeout(() => { sendQueued = false; node?.port.postMessage({ params: values.slice() }); }, 15);   // not rAF: it stops in background tabs
}

function fmtPlain(p, v) {
  if (p.kind === 'choice') return p.choices[Math.round(v)] ?? String(v);
  if (p.kind === 'bool') return v > 0.5 ? 'on' : 'off';
  if (p.kind === 'int') return `${Math.round(v)}${p.unit ? ' ' + p.unit : ''}`;
  return `${(+v).toFixed(Math.abs(p.max - p.min) >= 10 ? 1 : 2)}${p.unit ? ' ' + p.unit : ''}`;
}

function buildParams() {
  const box = $('params');
  box.replaceChildren();
  machine.info.params.forEach((p, i) => {
    const row = document.createElement('div');
    row.className = 'param';
    const label = document.createElement('label');
    label.textContent = p.name;
    label.htmlFor = 'p_' + p.id;
    let input;
    if (p.kind === 'choice') {
      input = document.createElement('select');
      p.choices.forEach((c, k) => input.add(new Option(c, String(k))));
      input.value = String(Math.round(values[i]));
      input.oninput = () => { values[i] = +input.value; changed(i); };
    } else if (p.kind === 'bool') {
      input = document.createElement('input');
      input.type = 'checkbox';
      input.checked = values[i] > 0.5;
      input.oninput = () => { values[i] = input.checked ? 1 : 0; changed(i); };
    } else {
      input = document.createElement('input');
      input.type = 'range';
      input.min = p.min; input.max = p.max;
      input.step = p.kind === 'int' ? 1 : (p.max - p.min) / 1000;
      input.value = values[i];
      input.oninput = () => { values[i] = +input.value; changed(i); };
      input.ondblclick = () => { values[i] = p.def; input.value = p.def; changed(i); };
    }
    input.id = 'p_' + p.id;
    const ro = document.createElement('span');
    ro.className = 'ro';
    ro.textContent = fmtPlain(p, values[i]);
    row.append(label, input, ro);
    box.append(row);
  });
}
function changed(i) {
  const p = machine.info.params[i];
  const ro = $('params').children[i].querySelector('.ro');
  if (!node) ro.textContent = fmtPlain(p, values[i]);
  sendValues();
}
// ---------- hardware panel ----------
const IKEY = () => `kit-web:${id}:inputs`;
const MKEY = 'kit-web:editor';
function loadInputs() {
  let saved = {};
  try { saved = JSON.parse(localStorage.getItem(IKEY()) || '{}'); } catch {}
  return Object.fromEntries(layout.inputs.map((i) => [i.id, i.id in saved ? saved[i.id] : i.def]));
}
const inputList = () => Object.entries(inputs).map(([k, v]) => [+k, v]);
function hardware() { try { return localStorage.getItem(MKEY) !== 'easy'; } catch { return true; } }
function showMode() {
  const hw = !!layout && hardware();
  $('modeBtn').hidden = !layout;
  $('modeBtn').textContent = hw ? 'Easy controls' : 'Hardware panel';
  $('hwCard').hidden = !hw;
  $('params').hidden = hw;
  $('textDisplay').hidden = hw;
  if (!hw) panel?.releaseAll();
}
function buildPanel() {
  panel?.destroy();
  panel = null;
  adoptSerial = -1;
  if (!layout) { showMode(); return; }
  panelDefs();
  inputs = loadInputs();
  const index = (pid) => machine.info.params.findIndex((p) => p.id === pid);
  panel = new Panel(layout, machine.info.params, {
    key: (k, down) => node?.port.postMessage({ key: [k, down] }),
    param: (pid, v) => { const i = index(pid); if (i >= 0) { values[i] = v; syncParamRow(i); changed(i); } },
    input: (k, v) => {
      inputs[k] = v;
      try { localStorage.setItem(IKEY(), JSON.stringify(inputs)); } catch {}
      node?.port.postMessage({ input: [k, v] });
    },
    getParam: (pid) => values[index(pid)] ?? 0,
    getInput: (k) => inputs[k] ?? 0,
  });
  $('hw').replaceChildren(panel.svg);
  $('hwNote').textContent = layout.notes || '';
  panel.update({ valid: false });
  showMode();
}
function syncParamRow(i) {
  const inp = $('params').children[i]?.querySelector('input,select');
  if (!inp) return;
  if (inp.type === 'checkbox') inp.checked = values[i] > 0.5; else inp.value = values[i];
}
// settings typed on the front panel become the parameter values (sent back unchanged: not retyped)
function showPanel(st) {
  if (!panel) return;
  panel.update(st);
  if (!st.valid || st.serial === adoptSerial) return;
  const first = adoptSerial < 0;
  adoptSerial = st.serial;
  if (first && st.serial === 0) return;
  let any = false;
  (st.adopt || []).forEach((v, i) => { if (v !== null && i < values.length && values[i] !== v) { values[i] = v; syncParamRow(i); any = true; } });
  if (any) { panel.sync(); sendValues(); }
}

function setAll(vals) {
  vals.forEach((v, i) => { if (i < values.length) values[i] = v; });
  buildParams();
  panel?.sync();
  sendValues();
}

function showReadouts(r) {
  $('panel').textContent = r.panel || ' ';
  $('mstatus').textContent = r.status || '';
  const rows = $('params').children;
  r.param.forEach((t, i) => {
    if (!rows[i]) return;
    rows[i].classList.toggle('inert', t === '');
    rows[i].querySelector('.ro').textContent = t === '' ? '—' : t;
  });
}

// ---------- ROMs ----------
function showRoms() {
  const ul = $('romSlots');
  ul.replaceChildren();
  rec.slots.forEach((s) => {
    const li = document.createElement('li');
    const a = document.createElement('span');
    a.textContent = s.role;
    const b = document.createElement('span');
    b.className = s.found ? (s.known ? 'ok' : 'warn') : 'bad';
    b.textContent = s.found ? (s.known ? `${s.label}` : `${s.file} (not a known dump)`) : 'missing';
    li.append(a, b);
    ul.append(li);
  });
  const sizes = machine.info.roms.map((r) => `${r.role} (${r.sizes.join(' or ')} bytes)`).join('; ');
  $('romNote').textContent = rec.complete
    ? 'Stored in this browser only, never uploaded.'
    : `Bring your own: drop the image files anywhere on the page, or use Add files. Wanted: ${sizes}. They stay in this browser and are never uploaded.`;
  $('power').disabled = !rec.complete;
}
async function addRomFiles(list) {
  const fresh = [];
  for (const f of list) {
    if (!machine.sizeFits(f.size)) continue;
    fresh.push({ name: f.name, bytes: new Uint8Array(await f.arrayBuffer()) });
  }
  if (!fresh.length) { $('romNote').textContent = 'None of those files has a size this machine uses.'; return; }
  const names = new Set(fresh.map((f) => f.name));
  files = files.filter((f) => !names.has(f.name)).concat(fresh);
  rec = machine.recognise(files);
  // keep only files that fill a slot
  const used = new Set(rec.slots.filter((s) => s.found).map((s) => s.file));
  files = files.filter((f) => used.has(f.name));
  await saveFiles(id, files).catch(() => {});
  showRoms();
  if (node && rec.complete) restart();
}

// ---------- audio ----------
async function start() {
  if (!ctx) {
    ctx = new AudioContext({ latencyHint: 'playback' });
    master = ctx.createGain(); master.connect(ctx.destination);
    dryGain = ctx.createGain(); dryGain.connect(master);
    wetGain = ctx.createGain(); wetGain.connect(master);
    sourceBus = ctx.createGain(); sourceBus.connect(dryGain);
    analyser = ctx.createAnalyser(); analyser.fftSize = 2048; wetGain.connect(analyser);
    meter();
    mix();
  }
  await ctx.resume();
  await ctx.audioWorklet.addModule(`worklet-${id}.mjs`);
  node = new AudioWorkletNode(ctx, `kit-${id}`, {
    numberOfInputs: 1, numberOfOutputs: 1, outputChannelCount: [2], channelCount: 2, channelCountMode: 'explicit',
    processorOptions: { files, params: values.slice(), inputs: layout ? inputList() : [] },
  });
  $('audioStatus').textContent = 'booting…';
  node.port.onmessage = (e) => {
    const d = e.data;
    if (d.type === 'ready') {
      $('audioStatus').textContent = `${ctx.sampleRate} Hz, machine latency ${d.latency} samples (${(d.latency / ctx.sampleRate * 1000).toFixed(1)} ms)` +
        `, output ${((ctx.outputLatency || ctx.baseLatency || 0) * 1000).toFixed(0)} ms`;
      showPresets(d.presets);
    } else if (d.type === 'error') {
      $('audioStatus').textContent = 'cannot run: ' + d.message;
    } else if (d.type === 'readouts') showReadouts(d.readouts);
    else if (d.type === 'panel') showPanel(d.panel);
  };
  sourceBus.connect(node);
  node.connect(wetGain);
  await setSource($('source').value);
  $('power').textContent = 'Stop audio';
  $('power').classList.add('on');
}
function stopMachine() {
  if (!node) return;
  panel?.releaseAll();
  adoptSerial = -1;
  node.port.postMessage({ stop: true });
  try { sourceBus.disconnect(node); } catch {}
  node.disconnect();
  node = null;
}
async function stop() {
  stopMachine();
  stopSource();
  await ctx?.suspend();
  $('power').textContent = 'Start audio';
  $('power').classList.remove('on');
  $('audioStatus').textContent = '';
  $('panel').textContent = ' ';
  $('mstatus').textContent = '';
  buildParams();
  panel?.update({ valid: false });
}
async function restart() { stopMachine(); await start(); }

function stopSource() {
  if (srcNode) { try { srcNode.stop?.(); } catch {} srcNode.disconnect(); srcNode = null; }
  if (micStream) { micStream.getTracks().forEach((t) => t.stop()); micStream = null; }
}
async function setSource(kind) {
  src = kind;
  if (!ctx || !node) return;
  stopSource();
  if (kind === 'none') return;
  if (kind === 'mic') {
    try {
      micStream = await navigator.mediaDevices.getUserMedia({ audio: { echoCancellation: false, noiseSuppression: false, autoGainControl: false } });
      srcNode = ctx.createMediaStreamSource(micStream);
    } catch (e) { $('audioStatus').textContent = 'no input: ' + e.message; return; }
  } else {
    const buf = kind === 'file' ? fileBuffer : makeSource(ctx, kind);
    if (!buf) { $('audioFile').click(); return; }
    srcNode = ctx.createBufferSource();
    srcNode.buffer = buf;
    srcNode.loop = true;
    srcNode.start();
  }
  srcNode.connect(sourceBus);
}

// the machine's output after Wet, before Volume: peak over the last ~40 ms, held for a moment
let hold = -120, holdAt = 0;
function meter() {
  const buf = new Float32Array(analyser.fftSize);
  const tick = () => {
    analyser.getFloatTimeDomainData(buf);
    let pk = 0;
    for (const x of buf) pk = Math.max(pk, Math.abs(x));
    const db = pk > 0 ? 20 * Math.log10(pk) : -120;
    const now = performance.now();
    if (db >= hold || now - holdAt > 800) { hold = db; holdAt = now; }
    const el = $('meter');
    el.style.setProperty('--level', `${Math.max(0, Math.min(100, (db + 60) / 60 * 100))}%`);
    el.dataset.clip = hold > -0.1 ? '1' : '';
    $('meterOut').textContent = node ? (hold <= -90 ? 'silent' : `${hold.toFixed(1)} dB`) : '';
    requestAnimationFrame(tick);
  };
  tick();
}

function mix() {
  for (const k of ['dry', 'wet', 'vol']) $(k + 'Out').textContent = (+$(k).value).toFixed(2);
  if (!ctx) return;
  dryGain.gain.value = +$('dry').value;
  wetGain.gain.value = +$('wet').value;
  master.gain.value = (+$('vol').value) ** 2;
}

function showPresets(ps) {
  const sel = $('presets');
  sel.replaceChildren(new Option('Presets…', ''));
  ps.forEach((p, k) => sel.add(new Option(p.name, String(k))));
  sel.hidden = ps.length === 0;
  sel.onchange = () => { if (sel.value !== '') setAll(ps[+sel.value].values); sel.value = ''; };
}

// ---------- machine switch ----------
async function choose(mid) {
  if (node) { stopMachine(); stopSource(); }
  id = mid;
  try { localStorage.setItem('kit-web:last', id); } catch {}
  if (location.hash !== '#' + id) history.replaceState(null, '', '#' + id);
  for (const a of $('machines').children) a.classList.toggle('on', a.dataset.id === id);
  $('name').textContent = MACHINES.find((m) => m.id === id).label;
  $('about').textContent = 'loading…';
  const create = (await import(`./machines/${id}.js`)).default;
  machine = await Machine.load(create);
  document.title = `${machine.info.name}`;
  $('name').textContent = machine.info.name;
  $('about').textContent = machine.info.about;
  values = loadValues();
  buildParams();
  layout = machine.panelLayout();
  buildPanel();
  $('presets').hidden = true;
  files = await loadFiles(id);
  rec = machine.recognise(files);
  showRoms();
  $('panel').textContent = ' ';
  $('mstatus').textContent = '';
  $('audioStatus').textContent = '';
  if (ctx && ctx.state === 'running' && rec.complete) await start();
  else { $('power').textContent = 'Start audio'; $('power').classList.remove('on'); }
}

// ---------- wiring ----------
for (const m of MACHINES) {
  const a = document.createElement('a');
  a.href = '#' + m.id; a.textContent = m.label; a.dataset.id = m.id;
  a.onclick = (e) => { e.preventDefault(); if (m.id !== id) choose(m.id); };
  $('machines').append(a);
}
$('power').onclick = () => (node ? stop() : start().catch((e) => { $('audioStatus').textContent = String(e); }));
$('source').onchange = () => setSource($('source').value);
$('audioFile').onchange = async (e) => {
  const f = e.target.files[0];
  if (!f) return;
  if (!ctx) ctx = new AudioContext();
  try { fileBuffer = await ctx.decodeAudioData(await f.arrayBuffer()); }
  catch { $('audioStatus').textContent = 'could not decode ' + f.name; return; }
  $('source').value = 'file';
  setSource('file');
};
for (const k of ['dry', 'wet', 'vol']) $(k).oninput = mix;
$('romFiles').onchange = (e) => addRomFiles(e.target.files);
$('romForget').onclick = async () => { files = []; await saveFiles(id, files).catch(() => {}); rec = machine.recognise(files); showRoms(); if (node) stop(); };
$('modeBtn').onclick = () => { try { localStorage.setItem(MKEY, hardware() ? 'easy' : 'hardware'); } catch {} showMode(); };
$('reset').onclick = () => setAll(machine.info.params.map((p) => p.def));
addEventListener('dragover', (e) => { e.preventDefault(); document.body.classList.add('drop'); });
addEventListener('dragleave', (e) => { if (!e.relatedTarget) document.body.classList.remove('drop'); });
addEventListener('drop', (e) => { e.preventDefault(); document.body.classList.remove('drop'); addRomFiles(e.dataTransfer.files); });
addEventListener('hashchange', () => { const m = location.hash.slice(1); if (m !== id && MACHINES.some((x) => x.id === m)) choose(m); });
mix();
choose(id);
