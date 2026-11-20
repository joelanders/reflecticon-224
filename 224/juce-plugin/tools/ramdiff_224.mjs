#!/usr/bin/env node
// Research probe: boot an original-224 set, then press front-panel buttons
// and print which RAM bytes (0x3800-0x3FFF) change after each press.
//   node tools/ramdiff_224.mjs ROM_SET_DIRECTORY "bank:mask,bank:mask,..."
import { readdirSync, readFileSync } from 'node:fs';
import { join, dirname } from 'node:path';
import { fileURLToPath } from 'node:url';

const here = dirname(fileURLToPath(import.meta.url));
const web = join(here, '..', '..', 'web-demo', 'page');
const { default: createLexicon } = await import(join(web, 'lexicon224x.js'));
const { createMachine, chipBase, modelOf } = await import(join(web, 'larc.js'));
const { createPanel224Operator } = await import(join(web, 'panel224.js'));

const directory = process.argv[2];
const presses = (process.argv[3] || '').split(',').filter(Boolean).map((s) => s.split(':').map(Number));
const names = readdirSync(directory).filter((n) => chipBase(n) !== null);
const chips = names.map((n) => ({ base: chipBase(n), bytes: new Uint8Array(readFileSync(join(directory, n))) }))
  .sort((a, b) => a.base - b.base);
const Module = await createLexicon();
const m = createMachine(Module, { soon: setImmediate });
m.onFail = () => { console.log('stopped'); process.exit(1); };
m.powerOn(chips, modelOf(names));
const op = createPanel224Operator(m);
await m.sleep(9);
const snap = () => Array.from({ length: 0x800 }, (_, i) => m.peek(0x3800 + i));
let before = snap();
console.log(`booted: "${op.display().top}"`);
for (const [bank, mask] of presses) {
  m.schedule(m.time, () => m.api.button(m.handle, bank, mask));
  await m.sleep(0.3);
  const held = snap();
  m.schedule(m.time, () => m.api.button(m.handle, bank, 0));
  await m.sleep(2);
  const after = snap();
  const d = [];
  for (let i = 0; i < 0x800; i++) {
    if (held[i] !== before[i] || after[i] !== before[i]) {
      d.push(`${(0x3800 + i).toString(16)}:${before[i].toString(16)}>${held[i].toString(16)}>${after[i].toString(16)}`);
    }
  }
  console.log(`press ${bank}:${mask.toString(16)} display "${op.display().top}" changes (${d.length}): ${d.slice(0, 60).join(' ')}`);
  before = after;
}
