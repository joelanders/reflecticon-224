#!/usr/bin/env node
// The 224 operator calls that the timeline does not exercise, recorded from
// the JS for comparison with the C++ port (panel224_extras.cpp): toggles,
// reading every page, a SHIFT-page move, a pre-delay move, sweeps, variations,
// and the operator's own error path. Writes the machine's inputs (format of
// ../../bench/record_timeline.mjs) and a results file, one line per call.
//
//   node tests/operator_equiv/panel224_record_extras.mjs ROM_SET_DIR CATALOG_DIRS OUT.events OUT.results [panel224|layout]
// panel224: rows-sv page/panel224.js (v4 sets only; the default for v4);
// layout: panel224_ops.mjs (tools/panel224_layouts.mjs; the default for v3.2).
import { createHash } from 'node:crypto';
import { existsSync, readdirSync, readFileSync, writeFileSync } from 'node:fs';
import { join, dirname } from 'node:path';
import { fileURLToPath } from 'node:url';
import { createPanel224LayoutOperator, layoutFor } from './panel224_ops.mjs';

const rows = join(dirname(fileURLToPath(import.meta.url)), '../../../web-demo');
const { default: createLexicon } = await import(join(rows, 'page/lexicon224x.js'));
const { createMachine, chipBase, modelOf, RATE } = await import(join(rows, 'page/larc.js'));
const { createPanel224Operator } = await import(join(rows, 'page/panel224.js'));

const [directory, catalogDirs, eventsPath, resultsPath, which] = process.argv.slice(2);
if (!resultsPath) {
  console.error('usage: node panel224_record_extras.mjs ROM_SET_DIR CATALOG_DIRS OUT.events OUT.results [panel224|layout]');
  process.exit(2);
}
const names = readdirSync(directory).filter((n) => chipBase(n) !== null);
const chips = names.map((n) => ({ base: chipBase(n), bytes: new Uint8Array(readFileSync(join(directory, n))) }))
  .sort((a, b) => a.base - b.base);
const hash = createHash('sha256');
for (const c of chips) {
  hash.update(c.bytes);
}
const rom = hash.digest('hex').slice(0, 16);
const catalogPath = catalogDirs.split(':').map((d) => join(d, `${rom}.json`)).find((p) => existsSync(p));
const catalog = JSON.parse(readFileSync(catalogPath, 'utf8'));
const L = layoutFor(catalog);

const m = createMachine(await createLexicon(), { soon: setImmediate });
m.onFail = () => { throw new Error('the machine stopped'); };
m.powerOn(chips, modelOf(names));
const events = [], results = [];
const frame = () => Math.round(m.time * RATE);
for (const kind of ['key', 'fader', 'poke', 'button', 'pot']) {
  const original = m.api[kind];
  m.api[kind] = (h, a, b) => { events.push(`${frame()} ${kind} ${a} ${b}`); return original(h, a, b); };
}
const mark = (name) => events.push(`${frame()} mark ${name}`);
const say = (text) => results.push(`${frame()} ${text}`);
const toggles = (t) => ['DYN DECAY', 'MODE ENH', 'DECAY OPT'].map((l) => (l in t ? (t[l] ? 1 : 0) : -1)).join(' ');
const attempt = async (label, fn) => {
  try {
    say(`${label} ${JSON.stringify(await fn())}`);
  } catch (e) {
    say(`${label} threw ${e.message}`);
  }
};

await m.sleep(16);
mark('boot');
let op;
let kind = which || (L.name === 'v4' ? 'panel224' : 'layout');
if (kind === 'panel224') {
  op = createPanel224Operator(m);
} else {
  op = createPanel224LayoutOperator(m, L);
}
say(`operator ${kind} layout ${L.name}`);
say(`select ${await op.selectProgram({ identity: 2 })}`);
mark('select');
if (op.readToggles) {
  const t = await op.readToggles();
  say(`toggles ${toggles(t)}`);
  say(`set MODE ENH ${toggles(await op.setToggle('MODE ENH', !t['MODE ENH']))}`);
  say(`set DECAY OPT ${toggles(await op.setToggle('DECAY OPT', !t['DECAY OPT']))}`);
  say(`set DECAY OPT ${toggles(await op.setToggle('DECAY OPT', t['DECAY OPT']))}`);
  mark('toggles');
}
for (const page of await op.readPages()) {
  say(`page ${page.page} [${page.heading}] column ${page.column}: ` +
    page.sliders.map((s) => `${s.name}=${s.value}/${s.raw}`).join(', '));
}
mark('pages');
await attempt('move 1 6 40', () => op.moveSlider(1, 5, 40));
await attempt('move 1 5 253', () => op.moveSlider(1, 4, 253));
await attempt('move 1 5 253', () => op.moveSlider(1, 4, 253));
if (L.diffusion) {
  await attempt('move 2 5 100', () => op.moveSlider(2, 4, 100));
}
await attempt('move 2 1 10', () => op.moveSlider(2, 0, 10));
await attempt('value 1 3', () => op.readValue(1, 2));
mark('moves');
await attempt('sweep 1 5', () => op.sweep(1, 4));
if (L.diffusion) {
  await attempt('sweep 2 5', () => op.sweep(2, 4));
} else {
  await attempt('sweep 1 2', () => op.sweep(1, 1));
}
mark('sweeps');
await attempt('variation 1', () => op.loadVariation(1));
await attempt('variation 2', () => op.loadVariation(2));
await attempt('stored', async () => [0, 1, 2, 3, 4, 5].map((s) => op.stored(0, s)).concat([op.stored(1, 4)]));
mark('variations');
events.push(`end ${frame()}`);
writeFileSync(eventsPath, events.join('\n') + '\n');
writeFileSync(resultsPath, results.join('\n') + '\n');
console.log(`${eventsPath}: ${events.length} events, end ${m.time.toFixed(1)} s`);
m.api.destroy(m.handle);
process.exit(0);
