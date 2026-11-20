#!/usr/bin/env node
// Boot one original-224 ROM set on the row machine's WASM build (the web
// demo's, read-only from ../web-demo/page) and report what it does:
// the display over the first seconds, then which PROGRAM buttons load,
// with each program's displayed parameter values.
//
//   node tools/probe_224_set.mjs ROM_SET_DIRECTORY [seconds]
//
// It drives the web's panel224.js, so the RAM it reads is v4.x's (v4.3,
// v4.4, TEST); on v3.2 the loads report NOT loaded (see panel224_layouts.mjs).
import { readdirSync, readFileSync } from 'node:fs';
import { join, dirname } from 'node:path';
import { fileURLToPath } from 'node:url';

const here = dirname(fileURLToPath(import.meta.url));
const web = join(here, '..', '..', 'web-demo', 'page');
const { default: createLexicon } = await import(join(web, 'lexicon224x.js'));
const { createMachine, chipBase, modelOf } = await import(join(web, 'larc.js'));
const { createPanel224Operator, PANEL224 } = await import(join(web, 'panel224.js'));

const directory = process.argv[2];
const seconds = +(process.argv[3] || 12);
const names = readdirSync(directory).filter((n) => chipBase(n) !== null);
const chips = names.map((n) => ({ base: chipBase(n), bytes: new Uint8Array(readFileSync(join(directory, n))) }))
  .sort((a, b) => a.base - b.base);
const model = modelOf(names);
const Module = await createLexicon();
const m = createMachine(Module, { soon: setImmediate });
let failed = false;
m.onFail = () => { failed = true; };
m.powerOn(chips, model);
const op = createPanel224Operator(m);
const hex = (a, n) => Array.from({ length: n }, (_, i) => m.peek(a + i).toString(16).padStart(2, '0')).join(' ');
let last = null;
for (let t = 0; t < seconds * 4; t++) {
  await m.sleep(0.25);
  const d = op.display().top;
  if (d !== last) { console.log(`t=${m.time.toFixed(2)}s display "${d}" 3F65..=${hex(PANEL224.PROGRAM, 12)} mode=${m.peek(PANEL224.MODE)}`); last = d; }
  if (failed) { console.log('the machine stopped'); process.exit(1); }
}
console.log(`cycles ${m.api.cycles(m.handle)}`);
for (let b = 0; b < 8; b++) {
  const ok = b < 6 ? await op.loadProgram(1 << b) : false;
  if (b >= 6) { continue; }
  const shown = ok ? op.display().top : '-';
  let values = [];
  if (ok) {
    for (let slot = 0; slot < 6; slot++) {
      try { values.push(await op.readValue(1, slot)); } catch (e) { values.push(`ERR ${e.message}`); }
    }
  }
  console.log(`PROG ${b + 1}: ${ok ? 'loaded' : 'NOT loaded'} display "${shown}" 3F65..=${hex(PANEL224.PROGRAM, 12)} values [${values.join(' | ')}]`);
}
