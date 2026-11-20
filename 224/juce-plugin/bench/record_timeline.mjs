#!/usr/bin/env node
// Record an operator timeline for the M0 speed gate: power on, boot, load a
// program, sweep one slider, load a second program. The page's own operators
// do the work (page/larc.js for the 224XL, page/panel.js for the 224X,
// page/panel224.js for the 224) on the WASM build, and every input they give
// the machine is written with the 48 kHz frame it precedes, in the format of
// ../../web-demo/tests/real_ir_setup.mjs plus two kinds for the
// front panels:
//   "frame key CODE DOWN" / "frame fader SLOT VALUE" / "frame poke ADDRESS VALUE"
//   "frame button BANK MASK" / "frame pot POT VALUE"
//   "frame mark NAME"   a phase boundary (boot, load_a, sweep, load_b)
//   "end FRAME"
//
//   node bench/record_timeline.mjs ROM_SET_DIR OUT.events FIRST SECOND
// FIRST, SECOND: program names from the set's catalog (page/catalogs/).
import { createHash } from 'node:crypto';
import { readdirSync, readFileSync, writeFileSync } from 'node:fs';
import { join, dirname } from 'node:path';
import { fileURLToPath } from 'node:url';

const rows = join(dirname(fileURLToPath(import.meta.url)), '../../web-demo');
const { default: createLexicon } = await import(join(rows, 'page/lexicon224x.js'));
const { createMachine, createOperator, chipBase, modelOf, RATE } = await import(join(rows, 'page/larc.js'));
const { createPanelOperator } = await import(join(rows, 'page/panel.js'));
const { createPanel224Operator } = await import(join(rows, 'page/panel224.js'));

const [directory, outPath, firstName, secondName] = process.argv.slice(2);
if (!secondName) {
  console.error('usage: node bench/record_timeline.mjs ROM_SET_DIR OUT.events FIRST SECOND');
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
const catalog = JSON.parse(readFileSync(join(rows, `page/catalogs/${rom}.json`), 'utf8'));
const entry = (name) => {
  const found = catalog.programs.find((p) => p.name === name);
  if (!found) {
    throw new Error(`${name}: not in the catalog (${catalog.programs.map((p) => p.name).join(', ')})`);
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
let op;
if (model === 1) {
  op = createPanel224Operator(m);
} else if (m.api.larcConnected(m.handle)) {
  op = createOperator(m);
} else {
  op = createPanelOperator(m);
}
const select = async (e) => {
  if (op.kind === 'panel') {
    return op.selectProgram(e);
  }
  for (let attempt = 0; attempt < 3; attempt++) {
    if (attempt) {
      await op.cancelShift();
      await m.sleep(0.5);
      op.current.bank = 0;
    }
    if (await op.selectProgram(e.bank, e.program)) {
      return true;
    }
  }
  return false;
};

if (!await select(entry(firstName))) {
  throw new Error(`could not load ${firstName}`);
}
await m.sleep(1);
mark('load_a');
// Sweep the first slider of page 1 across its range, as a user dragging it.
const moves = [];
for (const raw of [32, 96, 160, 224, 128, 64]) {
  moves.push(await op.moveSlider(1, 0, raw));
  await m.sleep(0.2);
}
await m.sleep(1);
mark('sweep');
if (!await select(entry(secondName))) {
  throw new Error(`could not load ${secondName}`);
}
await m.sleep(1);
mark('load_b');
events.push(`end ${frame()}`);
writeFileSync(outPath, events.join('\n') + '\n');
console.log(`${outPath}: ${firstName} -> ${secondName}, sweep echoes ${JSON.stringify(moves)}, ${events.length} events, end ${(m.time).toFixed(1)} s`);
m.api.destroy(m.handle);
process.exit(0);
