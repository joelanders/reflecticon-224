// node web/tests/node_check.mjs ID ROM_DIR [seconds]: the page's machine module in Node, through the
// same C API the AudioWorklet uses. Boots from the ROM folder, sets the defaults, renders noise at
// 48 kHz in 128-frame blocks, checks the output, prints readouts and speed.
import fs from 'node:fs';
import path from 'node:path';
import { fileURLToPath } from 'node:url';
import { Machine } from '../page/machine.mjs';

const [id, dir, secs = '5'] = process.argv.slice(2);
const here = path.dirname(fileURLToPath(import.meta.url));
const create = (await import(path.join(here, '..', 'page', 'machines', id + '.js'))).default;
const m = await Machine.load(create);
const files = fs.readdirSync(dir).filter((f) => fs.statSync(path.join(dir, f)).isFile())
  .map((f) => ({ name: f, bytes: new Uint8Array(fs.readFileSync(path.join(dir, f))) }));
const rec = m.recognise(files);
console.log(m.info.name + ': ' + rec.describe);
let fails = 0;
const check = (ok, what) => { console.log(`  ${ok ? 'ok  ' : 'FAIL'} ${what}`); if (!ok) fails++; };
check(rec.complete, 'ROMs recognised');
const err = m.boot(files, 48000);
check(!err, 'boots' + (err ? ': ' + err : ''));
if (err) process.exit(1);
m.setParams(m.info.params.map((p) => p.def));
let seed = 1;
const rnd = () => ((seed = (seed * 1103515245 + 12345) >>> 0) / 2 ** 32 - 0.5) * 0.2;
const n = 128, blocks = Math.round(Number(secs) * 48000 / n);
const inL = new Float32Array(n), inR = new Float32Array(n), outL = new Float32Array(n), outR = new Float32Array(n);
let e = 0, finite = true;
const t0 = performance.now();
for (let k = 0; k < blocks; k++) {
  for (let i = 0; i < n; i++) { inL[i] = k < blocks / 2 ? rnd() : 0; inR[i] = inL[i]; }
  m.process(inL, inR, outL, outR, n);
  for (let i = 0; i < n; i++) { e += outL[i] * outL[i] + outR[i] * outR[i]; finite &&= Number.isFinite(outL[i] + outR[i]); }
}
const s = (performance.now() - t0) / 1000;
check(finite && e > 1e-6, `output finite and not silent (energy ${e.toExponential(2)})`);
const r = m.readouts();
check(r.param.length === m.info.params.length, `readouts for ${r.param.length} params; panel "${r.panel}"`);
console.log(`  latency ${m.latency()} samples; ${secs} s in ${s.toFixed(2)} s = ${(Number(secs) / s).toFixed(1)}x real time; ${m.presets().length} presets`);
process.exit(fails ? 1 : 0);
