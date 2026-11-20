// Page UI for the row machine. The machine driver and the two operators live
// in larc.js (the 224XL's LARC) and panel.js (the 224X's front panel), shared
// with the Node precompute; this file is the page: firmware loading, audio,
// the program dropdown, the slider columns (params.js), the checkboxes and the
// diagnostic views.
import createLexicon from './lexicon224x.js';
import { RATE, createMachine, createOperator } from './larc.js';
import { createPanelOperator } from './panel.js';
import { saveFirmware, loadFirmware, forgetFirmware } from './firmware_store.js';
import { expandArchives, findSets, describeSet, setHash, devServerFiles, newestSet } from './firmware.js';
import { shareLink, readShareLink, applyShareLink } from './share.js';

// A share link (share.js) waiting for its firmware: when the loaded chips
// match its hash, the page powers on and applies it.
let pendingShare = readShareLink(location.search);
import { createPanel224Operator } from './panel224.js';
import { createInput } from './input.js';
import { programKey, selectProgram, readCatalog, fillProgramMenu } from './programs.js';
import { createParams } from './params.js';

const $ = (id) => document.getElementById(id);
let m = null, op = null, chips = [], chipModel = 0, romHash = '', booted = false;   // chipModel: 1 = the original 224

const Module = await createLexicon();
m = createMachine(Module);
m.onFail = () => { $('audioStatus').textContent = 'the machine stopped (see the console)'; stopAudio(); };
const larcOp = createOperator(m), panelOp = createPanelOperator(m), panel224Op = createPanel224Operator(m);
op = larcOp;

// =====================================================================
// Firmware: the sets found among the files the visitor gave (firmware.js),
// one chosen, and power-on.
// =====================================================================
let found = [], chosen = null;     // [{name, model, chips, unsupported?}], the one to boot

// Files from the picker, a drop, the dev server or this browser's memory.
async function addFiles(files, remember) {
  files = await expandArchives(files);
  const sets = await findSets(files);
  for (const set of sets) found = found.filter((s) => s.name !== set.name).concat([set]);
  if (remember && sets.some((set) => !set.name.startsWith('unrecognized'))) {
    await saveFirmware(files);
    $('savedRoms').hidden = false;
  }
  if (!sets.length) $('bootStatus').textContent = 'no Lexicon chips among those files';
  showSets((await newestSet(sets)) || chosen);
  return sets;
}

// The sets in the firmware dropdown; choosing one loads its chips.
function showSets(select) {
  const menu = $('set');
  menu.innerHTML = '';
  found.forEach((set, i) => {
    let label = `${set.name} (${describeSet(set)})`;
    if (set.unsupported) label = `${set.name} (not supported)`;
    const option = new Option(label, String(i), false, set === select);
    option.disabled = !!set.unsupported;
    if (set.unsupported) option.title = set.unsupported;
    menu.add(option);
  });
  menu.disabled = !found.length;
  if (select && !select.unsupported) choose(select);
}
$('set').onchange = () => choose(found[+$('set').value]);

async function choose(set) {
  chosen = set;
  chips = set.chips;
  chipModel = set.model;
  romHash = await setHash(chips);
  $('power').disabled = false;
  if (pendingShare && romHash === pendingShare.fw && !booted) $('power').click();
}

$('roms').onchange = async (event) => addFiles(await Promise.all(Array.from(event.target.files,
  async (file) => ({ name: file.name, bytes: new Uint8Array(await file.arrayBuffer()) }))), true);
$('drop').ondragover = (e) => { e.preventDefault(); $('drop').classList.add('over'); };
$('drop').ondragleave = () => $('drop').classList.remove('over');
$('drop').ondrop = async (e) => {
  e.preventDefault();
  $('drop').classList.remove('over');
  addFiles(await Promise.all(Array.from(e.dataTransfer.files,
    async (file) => ({ name: file.name, bytes: new Uint8Array(await file.arrayBuffer()) }))), true);
};
$('forgetRoms').onclick = async () => { await forgetFirmware(); $('savedRoms').hidden = true; };

// The dev server's sets (serve.py --roms), then this browser's remembered
// files. ?rom=SET chooses a set and ?boot=1 powers it on; a share link
// (share.js) chooses the set whose hash it names and powers on by itself;
// otherwise the newest set powers on by itself.
(async () => {
  await devServerFiles((files) => addFiles(files, false));
  const saved = await loadFirmware();
  if (saved) {
    await addFiles(saved.files, false);
    $('savedRoms').hidden = false;
  }
  const params = new URLSearchParams(location.search), wanted = params.get('rom');
  const pick = wanted && found.find((set) => set.name === wanted.replace(/_/g, '.'));
  if (pick) {
    showSets(pick);
    if (params.get('boot') && !pendingShare) { await choose(pick); $('power').click(); }
  } else if (!pendingShare) {
    const newest = await newestSet(found);
    showSets(newest);
    if (newest) { await choose(newest); $('power').click(); }
  }
  if (pendingShare && !booted) {
    for (const set of found) {
      await choose(set);
      if (romHash === pendingShare.fw) { showSets(set); return; }
    }
    $('bootStatus').textContent = `the shared link is for firmware ${pendingShare.fw}; drop that set's chips here`;
  }
})();

$('power').onclick = async () => {
  stopAudio();
  m.powerOn(chips, chipModel);
  m.api.setAnalog(m.handle, $('analog').checked ? 1 : 0);
  booted = false;
  $('start').disabled = true;
  const started = performance.now();
  // v8.1 runs its application after about 8 s; v8.21 has the LARC connected by about 15 s.
  $('bootStatus').textContent = 'booting…';
  await m.sleep(16);
  $('bootStatus').textContent = `booted: 16 s of machine time in ${((performance.now() - started) / 1000).toFixed(1)} s`;
  booted = true;
  $('power').textContent = 'Reboot';
  $('start').disabled = false;
  // The original 224 has its own operator (panel224.js); a 224XL firmware
  // talks to the LARC by now; a 224X firmware to the front panel.
  if (chipModel === 1) op = panel224Op;
  else op = m.api.larcConnected(m.handle) ? larcOp : panelOp;
  // The toggles are the LARC's, or the 224's PROGRAM 7 and 8.
  $('larcOptions').hidden = !op.readToggles;
  $('toggle-dyn').parentElement.hidden = $('mute').parentElement.hidden = op === panel224Op;
  operate(startEasyUI);
};

// =====================================================================
// Audio: a ScriptProcessor asks for blocks; the machine renders them.
// =====================================================================
let node = null, audio = null;
let renderWall = 0, renderMachine = 0;
// The page's AudioContext, made on first use (a click, a file, the microphone).
function audioContext() {
  audio = audio || new AudioContext({ sampleRate: RATE });
  return audio;
}
// The input sources and their controls (input.js, shared with compare.html).
const input = createInput(audioContext);

$('start').onclick = () => {
  audioContext().resume();   // a context created outside a click starts suspended
  const frames = 2048, b = m.scratch(frames);
  node = audio.createScriptProcessor(frames, 2, 2);
  node.onaudioprocess = (event) => {
    const inL = new Float32Array(Module.HEAPF32.buffer, b.inL, frames);
    const inR = new Float32Array(Module.HEAPF32.buffer, b.inR, frames);
    input.fill(event, inL, inR, frames);
    const dry = [Float32Array.from(inL), Float32Array.from(inR)];
    const t0 = performance.now();
    if (!m.renderInto(b, frames)) return;
    renderWall += (performance.now() - t0) / 1000;
    renderMachine += frames / RATE;
    const out = new Float32Array(Module.HEAPF32.buffer, b.out, frames * 4);
    const wet = $('wet').value / 100, cl = 'ABCD'.indexOf($('outL').value), cr = 'ABCD'.indexOf($('outR').value);
    const left = event.outputBuffer.getChannelData(0), right = event.outputBuffer.getChannelData(1);
    for (let f = 0; f < frames; f++) {
      left[f] = wet * out[4 * f + cl] + (1 - wet) * dry[0][f];
      right[f] = wet * out[4 * f + cr] + (1 - wet) * dry[1][f];
    }
  };
  node.connect(audio.destination);
  input.attach(node);
  m.audioRunning = true;
  $('start').disabled = true;
  $('stop').disabled = false;
};
function stopAudio() {
  if (node) node.disconnect();
  node = null;
  input.attach(null);
  m.audioRunning = false;
  $('start').disabled = !booted;
  $('stop').disabled = true;
  m.pump();
}
$('stop').onclick = stopAudio;

// Space starts or stops the audio, M mutes the input.
document.addEventListener('keydown', (e) => {
  // (a focused button, checkbox or field already handles its own keys)
  if (e.target.closest('input:not([type=range]), textarea, select, button') || e.metaKey || e.ctrlKey || e.altKey) return;
  if (e.code === 'Space') {
    e.preventDefault();
    if (!$('stop').disabled) stopAudio();
    else if (!$('start').disabled) $('start').click();
  } else if (e.code === 'KeyM') {
    input.toggleMute();
  }
});
// Analog boards on/off (analog): switch while listening to compare.
$('analog').onchange = (e) => m.handle && m.api.setAnalog(m.handle, e.target.checked ? 1 : 0);

// =====================================================================
// The easy UI: operator tasks run one at a time.
// =====================================================================
let operating = Promise.resolve(), catalog = [], precomputed = null;
function operate(task) {
  operating = operating.then(async () => {
    setBusy(true);
    try { await task(); } catch (e) { console.error(e); if (op.cancelShift) await op.cancelShift(); }
    setBusy(false);
  });
  return operating;
}
function setBusy(busy) {
  $('operatorStatus').textContent = busy ? `talking to the ${op.kind === 'panel' ? 'front panel' : 'LARC'}…` : '';
  for (const id of ['program', 'variation', 'toggle-dyn', 'toggle-enh', 'toggle-opt', 'mute', 'share'])
    $(id).disabled = busy || !catalog.length;
}

// Programs are keyed "B.P" on the LARC and by their button mask on the panel (programs.js).
const keyOf = (e) => programKey(op, e);
const selectEntry = (e) => selectProgram(op, e);

// The program list (programs.js): from a precomputed catalog, which also has
// every program's pages, slider value tables and variation presets, or
// scanned live.
async function startEasyUI() {
  ({ catalog, precomputed } = await readCatalog(op, romHash, (text) => { $('operatorStatus').textContent = text; }));
  fillProgramMenu($('program'), catalog, op);
  await selectEntry(catalog[0]);
  await describeProgram();
  if (op.readToggles) showToggles(await op.readToggles());
  if (pendingShare && pendingShare.fw === romHash) {
    const link = pendingShare;
    pendingShare = null;
    $('operatorStatus').textContent = 'applying the shared settings…';
    await applyShareLink(link, { catalog, keyOf, select: selectEntry, op });
    await describeProgram();
    if (op.readToggles) showToggles(await op.readToggles());
  }
  if (new URLSearchParams(location.search).get('selftest')) setTimeout(selfTest, 0);
}

// The link for the current state: the parameters' stored bytes straight from
// the firmware's RAM, compared with the variation's preset (share.js).
$('share').onclick = async () => {
  const program = precomputedProgram();
  if (!program) { $('operatorStatus').textContent = 'share links need a precomputed catalog for this firmware'; return; }
  const toggles = {};
  for (const [id, label] of TOGGLES) if (!$(id).parentElement.hidden && !$('larcOptions').hidden) toggles[label] = $(id).checked;
  const url = shareLink({ fw: romHash, key: currentKey(), program, variation: op.current.variation || 1,
    stored: (column, slot) => op.stored(column, slot), toggles });
  try {
    await navigator.clipboard.writeText(url);
    $('operatorStatus').textContent = 'share link copied';
  } catch {
    prompt('Share link:', url);
  }
};

const currentKey = () => op.kind === 'panel' ? `x${op.current.identity.toString(16)}` : `${op.current.bank}.${op.current.program}`;
const precomputedProgram = () => precomputed && precomputed.programs.find((p) => keyOf(p) === currentKey());

// The parameter sliders (params.js, shared with compare.html).
const params = createParams({ root: $('pages'), op: () => op, operate, known: () => precomputedProgram(),
  status: (text) => { $('operatorStatus').textContent = text; } });
const range = () => params.range();

// After a load: the parameters (params.js), then the variation menu.
async function describeProgram() {
  await params.describe();
  drawProgramControls();
}

function drawProgramControls() {
  const known = precomputedProgram(), select = $('variation');
  const variations = known ? known.variations : [1, 2, 3, 4, 5, 6, 7, 8, 9];
  if (select.options.length !== variations.length || [...select.options].some((o, i) => +o.value !== variations[i])) {
    select.innerHTML = '';
    for (const v of variations) select.add(new Option(`V${v}`, String(v)));
  }
  select.value = String(op.current.variation);
  $('program').value = currentKey();
}

const TOGGLES = [['toggle-dyn', 'DYN DECAY'], ['toggle-enh', 'MODE ENH'], ['toggle-opt', 'DECAY OPT']];
function showToggles(state) {
  for (const [id, label] of TOGGLES) if (label in state) $(id).checked = state[label];
}
for (const [id, label] of TOGGLES) $(id).onchange = (e) => operate(async () => showToggles(await op.setToggle(label, e.target.checked)));
$('mute').onchange = () => operate(async () => { $('mute').checked = await op.toggleMute(); });

$('program').onchange = (e) => {
  const entry = catalog.find((x) => keyOf(x) === e.target.value);
  params.setEnabled(false);
  operate(async () => {
    try {
      if (await selectEntry(entry)) await describeProgram();
      if (op === panel224Op) showToggles(await op.readToggles());   // a 224 load turns both on again
    } finally { params.setEnabled(true); }
  });
};
$('variation').onchange = (e) => {
  const variation = +e.target.value;
  params.setEnabled(false);
  operate(async () => {
    try { await op.loadVariation(variation); await describeProgram(); } finally { params.setEnabled(true); }
  });
};

// ?selftest=1: exercise the UI paths and print what the firmware answered.
async function selfTest() {
  const log = [], out = document.createElement('pre');
  out.id = 'selftest';
  document.body.appendChild(out);
  const note = (text) => { log.push(text); out.textContent = log.join('\n'); };
  const move = async (page, name, raw) => {
    const slot = params.pages[page - 1].sliders.findIndex((x) => x.name === name);
    const before = params.pages[page - 1].sliders[slot].value;
    let after = null;
    await operate(async () => { after = await op.moveSlider(page, slot, raw); });
    note(`page ${page} ${name}: ${before} -> ${after} (raw ${raw}); ${op.kind === 'panel' ? 'panel' : 'LARC'} now on page ${op.currentPage}`);
  };
  const [low, high] = range();
  if (op === panel224Op) { await selfTest224(note, move, low, high); return; }
  await move(2, 'DIFFUSION', high);
  await move(1, 'LF DECAY', 200);
  await move(2, 'DIFFUSION', low);
  const room = catalog.find((x) => x.name === 'ROOM');
  await operate(async () => { await selectEntry(room); await describeProgram(); });
  note(`program -> ${$('program').selectedOptions[0].textContent}; pages ${params.pages.length}; ` +
    `page 1: ${params.pages[0].sliders.map((x) => `${x.name} ${x.value}`).join(', ')}`);
  await operate(async () => { await op.loadVariation(2); await describeProgram(); });
  note(`variation 2: page 1: ${params.pages[0].sliders.map((x) => x.value).join(', ')}`);
  if (op.kind === 'panel') { note('selftest done'); return; }
  const before = $('toggle-dyn').checked;
  await operate(async () => showToggles(await op.setToggle('DYN DECAY', !before)));
  note(`dynamic decay: ${before} -> ${$('toggle-dyn').checked}`);
  await operate(async () => { $('mute').checked = await op.toggleMute(); });
  note(`mute: display "${op.display().top.trim()}", checkbox ${$('mute').checked}`);
  note('selftest done');
}

// The original 224: its SHIFT page, a decay pot, another program, and the
// PROGRAM 7 toggle.
async function selfTest224(note, move, low, high) {
  await move(2, 'DIFFUSION', high);
  await move(1, 'BASS', 200);
  await move(2, 'DIFFUSION', low);
  const hall = catalog.find((x) => x.name === 'SMALL CONCERT HALL A');
  await operate(async () => { await selectEntry(hall); await describeProgram(); showToggles(await op.readToggles()); });
  note(`program -> ${$('program').selectedOptions[0].textContent}; pages ${params.pages.length}; ` +
    `page 1: ${params.pages[0].sliders.map((x) => `${x.name} ${x.value}`).join(', ')}; ` +
    `page 2: ${params.pages[1].sliders.filter((x) => x.name !== 'INACTIVE').map((x) => `${x.name} ${x.value}`).join(', ')}`);
  const before = $('toggle-enh').checked;
  await operate(async () => showToggles(await op.setToggle('MODE ENH', !before)));
  note(`mode enhancement: ${before} -> ${$('toggle-enh').checked}`);
  note('selftest done');
}

// =====================================================================
// The views: headroom, delay memory and the running program.
// =====================================================================
// The input headroom meters: the FPC's two headroom registers (the firmware
// reads and restarts them; they hold the loudest level since). A lit segment
// is held for a moment so short peaks stay visible.
const HEADROOM = ['-24', '-18', '-12', '-6', '0 dB'], headroomHold = [[0, 0, 0, 0, 0], [0, 0, 0, 0, 0]];
for (const [channel, label] of [[0, 'L'], [1, 'R']]) {
  const row = document.createElement('div');
  row.innerHTML = `${label} ` + HEADROOM.map((text) => `<span>${text}</span>`).join('');
  $('headroom').appendChild(row);
}
// The gain ranger: which of its four gains each channel used lately.
const GAINS = ['0 dB', '+6', '+12', '+18'], gainHold = [[0, 0, 0, 0], [0, 0, 0, 0]];
for (const label of ['L', 'R']) {
  const row = document.createElement('div');
  row.innerHTML = `${label} ` + GAINS.map((text) => `<span>${text}</span>`).join('') + (label === 'L' ? ' gain range' : '');
  $('gains').appendChild(row);
}
function drawGains() {
  const used = m.api.inputGains(m.handle);
  [0, 1].forEach((channel) => {
    const segments = $('gains').children[channel].querySelectorAll('span');
    for (let g = 0; g < 4; g++) {
      if (used >> (4 * channel + g) & 1) gainHold[channel][g] = performance.now();
      segments[g].className = performance.now() - gainHold[channel][g] < 300 ? 'lit' : '';
    }
  });
}

function drawHeadroom() {
  [0, 1].forEach((channel) => {
    const held = m.api.headroom(m.handle, channel);
    const segments = $('headroom').children[channel].querySelectorAll('span');
    for (let k = 0; k < 5; k++) {
      // detector k (segment k from the left) = 24 - 6k dB below clipping
      if (!(held >> k & 1)) headroomHold[channel][k] = performance.now();
      const lit = performance.now() - headroomHold[channel][k] < 300;
      segments[k].className = lit ? (k === 4 ? 'lit clip' : 'lit') : '';
    }
  });
}

function refreshDisplays() {
  $('audioStatus').textContent = `machine time ${m.time.toFixed(1)} s` +
    (renderMachine ? `, rendering at ${(renderMachine / renderWall).toFixed(1)}x realtime` : '');
}
// The delay memory: 65,536 words on the 224X/224XL, the first 16,384 on the 224.
function drawMemory() {
  const canvas = $('memory'), g = canvas.getContext('2d');
  const size = m.api.model(m.handle) === 1 ? 16384 : 65536;
  const words = new Int16Array(Module.HEAP16.buffer, m.api.memory(m.handle), size);
  g.clearRect(0, 0, canvas.width, canvas.height);
  const step = size / canvas.width, mid = canvas.height / 2;
  for (let x = 0; x < canvas.width; x++) {
    let lo = 0, hi = 0;
    for (let i = x * step; i < (x + 1) * step; i++) { lo = Math.min(lo, words[i]); hi = Math.max(hi, words[i]); }
    g.fillRect(x, mid - hi / 32768 * mid, 1, Math.max(1, (hi - lo) / 32768 * mid));
  }
}
// The running program, as the C++ WCS disassembler reads it with the
// machine's own decode (../lens/wcs_disassembler.hpp).
// Which rows the firmware rewrote since the last look (LFOs, slider moves).
let previousWcs = null;
function drawRewrites() {
  const wcs = Array.from(new Uint32Array(Module.HEAPU32.buffer, m.api.wcs(m.handle), 128));
  if (previousWcs) {
    const rows = wcs.map((w, row) => w !== previousWcs[row] ? row : -1).filter((row) => row >= 0);
    $('rewrites').textContent = rows.length ? `rows the firmware rewrote in the last second: ${rows.join(' ')}` : '';
  }
  previousWcs = wcs;
}

// The running program, one line per row in the order it runs (row 0
// first): the row number and its decoded fields, without the raw word.
function drawProgram() {
  const listing = Module.UTF8ToString(m.api.listing(m.handle)).split('\n').filter(Boolean);
  $('wcs').textContent = listing.map((line) => {
    const x = /^\s*(\d+)\s+(?:step\s+\d+\s+)?[0-9a-f]{8}\s+(.*)$/.exec(line);
    return x ? `${x[1].padStart(3)}  ${x[2].trimEnd()}` : line.trimEnd();
  }).join('\n');
}
setInterval(() => { if (m.handle) { refreshDisplays(); drawMemory(); } }, 250);
setInterval(() => { if (m.handle && m.audioRunning) { drawHeadroom(); drawGains(); } }, 50);
setInterval(() => { if (m.handle) { drawProgram(); drawRewrites(); } }, 1000);
