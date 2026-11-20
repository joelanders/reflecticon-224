#!/usr/bin/env node
// ../../bench/record_timeline.mjs for the original 224 sets through the
// layout operator (panel224_ops.mjs: tools/panel224_layouts.mjs), so v3.2 and
// the TEST set get a JS timeline too. Same script, same output format:
// boot 16 s, load FIRST, pot 1 of page 1 to 32, 96, 160, 224, 128, 64, load
// SECOND. With the v4 layout on v4.3 it must reproduce tests/timelines/v43_*.
//
//   node tests/operator_equiv/panel224_record_timeline.mjs ROM_SET_DIR CATALOG_DIRS OUT.events FIRST SECOND
// CATALOG_DIRS: colon-separated directories searched for <hash>.json.
import { createHash } from 'node:crypto';
import { existsSync, readdirSync, readFileSync, writeFileSync } from 'node:fs';
import { join, dirname } from 'node:path';
import { fileURLToPath } from 'node:url';
import { createPanel224LayoutOperator, layoutFor } from './panel224_ops.mjs';

const rows = join(dirname(fileURLToPath(import.meta.url)), '../../../web-demo');
const { default: createLexicon } = await import(join(rows, 'page/lexicon224x.js'));
const { createMachine, chipBase, modelOf, RATE } = await import(join(rows, 'page/larc.js'));

const [directory, catalogDirs, outPath, firstName, secondName] = process.argv.slice(2);
if (!secondName) {
  console.error('usage: node panel224_record_timeline.mjs ROM_SET_DIR CATALOG_DIRS OUT.events FIRST SECOND');
  process.exit(2);
}
const names = readdirSync(directory).filter((n) => chipBase(n) !== null);
const model = modelOf(names);
const chips = names.map((n) => ({ base: chipBase(n), bytes: new Uint8Array(readFileSync(join(directory, n))) }))
  .sort((a, b) => a.base - b.base);
const hash = createHash('sha256');
for (const c of chips) {
  hash.update(c.bytes);
}
const rom = hash.digest('hex').slice(0, 16);
const catalogPath = catalogDirs.split(':').map((d) => join(d, `${rom}.json`)).find((p) => existsSync(p));
if (!catalogPath) {
  throw new Error(`no catalog for ${rom}`);
}
const catalog = JSON.parse(readFileSync(catalogPath, 'utf8'));
const entry = (name) => {
  const found = catalog.programs.find((p) => p.name === name);
  if (!found) {
    throw new Error(`${name}: not in the catalog`);
  }
  return found;
};

const Module = await createLexicon();
const m = createMachine(Module, { soon: setImmediate });
m.onFail = () => { throw new Error('the machine stopped'); };
m.powerOn(chips, model);
const events = [];
const frame = () => Math.round(m.time * RATE);
for (const kind of ['key', 'fader', 'poke', 'button', 'pot']) {
  const original = m.api[kind];
  m.api[kind] = (h, a, b) => { events.push(`${frame()} ${kind} ${a} ${b}`); return original(h, a, b); };
}
const mark = (name) => events.push(`${frame()} mark ${name}`);

await m.sleep(16);
mark('boot');
const L = layoutFor(catalog);
const op = createPanel224LayoutOperator(m, L);
if (!await op.selectProgram(entry(firstName))) {
  throw new Error(`could not load ${firstName}`);
}
await m.sleep(1);
mark('load_a');
const moves = [];
for (const raw of [32, 96, 160, 224, 128, 64]) {
  moves.push(await op.moveSlider(1, 0, raw));
  await m.sleep(0.2);
}
await m.sleep(1);
mark('sweep');
if (!await op.selectProgram(entry(secondName))) {
  throw new Error(`could not load ${secondName}`);
}
await m.sleep(1);
mark('load_b');
events.push(`end ${frame()}`);
writeFileSync(outPath, events.join('\n') + '\n');
console.log(`${outPath}: ${firstName} -> ${secondName} (layout ${L.name}), sweep echoes ${JSON.stringify(moves)}, ${events.length} events, end ${(m.time).toFixed(1)} s`);
m.api.destroy(m.handle);
process.exit(0);
