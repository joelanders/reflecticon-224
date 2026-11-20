#!/usr/bin/env node
// The LARC operator calls that record_timeline does not exercise, recorded
// from the JS (../../../web-demo/page/larc.js) for comparison with the
// C++ port (extras.cpp): toggles, mute, reading every page, a sweep, a
// variation and ALL SLIDERS. Writes the machine's inputs (format of
// ../../bench/record_timeline.mjs) and a results file of what the operator
// returned, one line per call.
//
//   node tests/operator_equiv/record_extras.mjs ROM_SET_DIR OUT.events OUT.results
import { readdirSync, readFileSync, writeFileSync } from 'node:fs';
import { join, dirname } from 'node:path';
import { fileURLToPath } from 'node:url';

const rows = join(dirname(fileURLToPath(import.meta.url)), '../../../web-demo');
const { default: createLexicon } = await import(join(rows, 'page/lexicon224x.js'));
const { createMachine, createOperator, chipBase, modelOf, RATE } = await import(join(rows, 'page/larc.js'));

const [directory, eventsPath, resultsPath] = process.argv.slice(2);
if (!resultsPath) {
  console.error('usage: node tests/operator_equiv/record_extras.mjs ROM_SET_DIR OUT.events OUT.results');
  process.exit(2);
}
const names = readdirSync(directory).filter((n) => chipBase(n) !== null);
const chips = names.map((n) => ({ base: chipBase(n), bytes: new Uint8Array(readFileSync(join(directory, n))) }))
  .sort((a, b) => a.base - b.base);
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
const toggles = (t) => ['DYN DECAY', 'MODE ENH', 'DECAY OPT'].map((l) => (l in t ? (t[l] ? 1 : 0) : -1)).join(' ');
const say = (text) => results.push(`${frame()} ${text}`);

await m.sleep(16);
mark('boot');
const op = createOperator(m);
say(`select ${await op.selectProgram(1, 1)}`);
mark('select');
const t = await op.readToggles();
say(`toggles ${toggles(t)}`);
say(`set DYN DECAY ${toggles(await op.setToggle('DYN DECAY', !t['DYN DECAY']))}`);
say(`mute ${await op.toggleMute()}`);
say(`mute ${await op.toggleMute()}`);
mark('toggles');
for (const page of await op.readPages()) {
  say(`page ${page.page} [${page.heading}] column ${page.column}: ` +
    page.sliders.map((s) => `${s.name}=${s.value}/${s.raw}`).join(', '));
}
mark('pages');
const table = await op.sweep(1, 1, 'MID DECAY');
say(`sweep ${table.map(([raw, text]) => `${raw}:${text}`).join(', ')}`);
mark('sweep');
say(`variation ${await op.loadVariation(2)}`);
say(`activate ${await op.activateSliders()}`);
mark('activate');
events.push(`end ${frame()}`);
writeFileSync(eventsPath, events.join('\n') + '\n');
writeFileSync(resultsPath, results.join('\n') + '\n');
console.log(`${eventsPath}: ${events.length} events, end ${m.time.toFixed(1)} s`);
m.api.destroy(m.handle);
process.exit(0);
