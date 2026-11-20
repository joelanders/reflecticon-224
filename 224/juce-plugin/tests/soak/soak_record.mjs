#!/usr/bin/env node
// A copy of ../../../web-demo/tests/soak.mjs (which this must not
// edit) that also records every input the machine is given, in the format of
// ../../bench/record_timeline.mjs plus a "FRAME mark aN KIND" line per action,
// so that the C++ soak (soak.cpp --events) can be compared frame for frame.
// The soak logic below is soak.mjs's, unchanged apart from the imports and
// the recording lines marked RECORD.
//
//   node tests/soak/soak_record.mjs ROM_SET_DIRECTORY [--actions 300] [--seed 1] [--events FILE]
//
// Original header:
// Soak test for the easy UI's operators: random program, variation and
// slider actions against the real firmware, as the page would issue them,
// checking after every action what the firmware actually holds.
//
//   node tests/soak.mjs ROM_SET_DIRECTORY [--actions 300] [--seed 1]
//
// Invariants, read from the firmware's RAM (not from its display):
//   - after a program or variation load, every parameter's stored byte is the
//     catalog's preset for it (the page's instant path relies on this);
//   - a slider move changes that parameter only: every other parameter of
//     the program, on every page, keeps its byte (the "muffled" bug broke this);
//   - (counted, not failed: the moved parameter may hold another value than
//     the one sent; the firmware enforces its own limits, e.g. minimum delays.)
// Snapshots are taken 1 s after a move: a SIZE move makes the firmware
// rebuild the program for ~0.4 s, using parts of RAM as scratch meanwhile.
// The emulator is deterministic, so a seed reproduces a failure exactly.
import { createHash } from 'node:crypto';
import { readdirSync, readFileSync, writeFileSync } from 'node:fs';
import { join, dirname } from 'node:path';
import { fileURLToPath } from 'node:url';
import createLexicon from '../../../web-demo/page/lexicon224x.js';
import { createMachine, createOperator, chipBase, modelOf, RATE } from '../../../web-demo/page/larc.js';
import { createPanelOperator } from '../../../web-demo/page/panel.js';
import { createPanel224Operator } from '../../../web-demo/page/panel224.js';

const here = dirname(fileURLToPath(import.meta.url));
const args = process.argv.slice(2);
const option = (name, fallback) => { const i = args.indexOf(name); return i >= 0 ? args.splice(i, 2)[1] : fallback; };
const actions = +option('--actions', 300);
const eventsPath = option('--events', null);   // RECORD
const verbose = args.includes('--verbose') && args.splice(args.indexOf('--verbose'), 1);
const say = (text) => { if (verbose) console.log(`  ${m.time.toFixed(1)} s: ${text} | ${op.display().top.trim()}`); };
let seed = +option('--seed', 1);
const random = () => { seed = (seed * 1103515245 + 12345) % 2147483648; return seed / 2147483648; };
const pick = (list) => list[Math.floor(random() * list.length)];

const directory = args[0];
const names = readdirSync(directory).filter((n) => chipBase(n) !== null && /\.BIN$/i.test(n));
const model = modelOf(names);
const chips = names
  .map((n) => ({ base: chipBase(n), bytes: new Uint8Array(readFileSync(join(directory, n))) })).sort((a, b) => a.base - b.base);
const hash = createHash('sha256');
for (const c of chips) hash.update(c.bytes);
const catalog = JSON.parse(readFileSync(join(here, '..', '..', '..', 'web-demo', 'page', 'catalogs', `${hash.digest('hex').slice(0, 16)}.json`), 'utf8'));

const m = createMachine(await createLexicon(), { soon: setImmediate });
m.powerOn(chips, model);
const events = [];   // RECORD
const frame = () => Math.round(m.time * RATE);   // RECORD
if (eventsPath) for (const kind of ['key', 'fader', 'poke', 'button', 'pot']) {   // RECORD
  const original = m.api[kind];
  m.api[kind] = (h, a, b) => { events.push(`${frame()} ${kind} ${a} ${b}`); return original(h, a, b); };
}
await m.sleep(16);
const op = model === 1 ? createPanel224Operator(m) : m.api.larcConnected(m.handle) ? createOperator(m) : createPanelOperator(m);
const panel = op.kind === 'panel';
const range = panel ? [0, 253] : [2, 254];
const load = (p) => panel ? op.loadProgram(p.identity) : op.selectProgram(p.bank, p.program);
const snapshot = (p) => p.pages.map((page) => page.sliders.map((_, slot) => op.stored(page.column, slot)));

let program = null, failures = 0;
// After a load every stored byte is the catalog's preset. (The original 224
// also lets a resting DEPTH or PRE-DELAY pot take over at a load, by its own
// pickup rule: panel224.js unexplainedLoadDifferences.)
let n = 0;
function loaded(p, v) {
  const bytes = snapshot(p);
  if (JSON.stringify(bytes) === JSON.stringify(p.raw[v])) return;
  const differences = op.unexplainedLoadDifferences ? op.unexplainedLoadDifferences(p, bytes) : [JSON.stringify(bytes)];
  if (differences.length) fail(`${n}: ${p.name} V${v} bytes differ from the catalog: ${differences.join(', ')}`);
  else counts.byPot++;
}
const counts = { program: 0, variation: 0, move: 0, adjusted: 0, byPot: 0 };
const started = Date.now(), machineStart = m.time;
const fail = (text) => { failures++; console.log(`FAIL #${failures}: ${text}`); };

for (n = 0; n < actions; n++) {
  const kind = !program ? 'program' : pick(['program', 'variation', 'move', 'move', 'move', 'move', 'move', 'move']);
  events.push(`${frame()} mark a${n} ${kind}`);   // RECORD
  if (kind === 'program') {
    const p = pick(catalog.programs);
    if (!await load(p)) { fail(`${n}: could not load ${p.name}`); continue; }
    say(`${n} program ${p.name}`);
    program = p;
    counts.program++;
    loaded(p, 1);
  } else if (kind === 'variation') {
    const v = pick(program.variations);
    if (!await op.loadVariation(v)) { fail(`${n}: ${program.name} V${v} did not load`); continue; }
    say(`${n} variation ${v}`);
    counts.variation++;
    loaded(program, v);
  } else {
    const choices = [];
    program.pages.forEach((page, i) => page.sliders.forEach((s, slot) => { if (s.name !== 'INACTIVE' && s.name) choices.push([i, slot]); }));
    const [i, slot] = pick(choices), value = range[0] + Math.floor(random() * (range[1] - range[0] + 1));
    const before = snapshot(program);
    let echo;
    try { echo = await op.moveSlider(program.pages[i].page, slot, value); }
    catch (e) { fail(`${n}: ${program.name} page ${i + 1} slot ${slot + 1} -> ${value}: ${e.message}`); continue; }
    counts.move++;
    say(`${n} move page ${i + 1} ${program.pages[i].sliders[slot].name} -> ${value}: ${echo}`);
    await m.sleep(1);
    const after = snapshot(program);
    if (after[i][slot] !== value) counts.adjusted++;
    after[i][slot] = before[i][slot];
    const changed = [];
    // INACTIVE slots are not controls; on SIZE pages they hold the firmware's
    // own storage for the neighbouring parameter.
    after.forEach((row, pi) => row.forEach((b, k) => { if (b !== before[pi][k] && program.pages[pi].sliders[k].name !== 'INACTIVE') changed.push(`page ${pi + 1} ${program.pages[pi].sliders[k].name} ${before[pi][k]}->${b}`); }));
    if (changed.length) fail(`${n}: moving ${program.name} page ${i + 1} ${program.pages[i].sliders[slot].name} also changed ${changed.join(', ')}`);
    if (echo === null) fail(`${n}: no echo for ${program.pages[i].sliders[slot].name}`);
  }
}
const machine = m.time - machineStart;
if (eventsPath) { events.push(`end ${frame()}`); writeFileSync(eventsPath, events.join('\n') + '\n'); }   // RECORD
console.log(`${actions} actions (${counts.program} programs, ${counts.variation} variations, ${counts.move} moves, ` +
  `${counts.adjusted} held another value than sent${counts.byPot ? `, ${counts.byPot} loads where a resting pot took over` : ''}): ${failures} failures; ${machine.toFixed(0)} s of machine time ` +
  `(${(machine / actions).toFixed(2)} s per action average), ${((Date.now() - started) / 1000).toFixed(0)} s wall`);
process.exit(failures ? 1 : 0);
