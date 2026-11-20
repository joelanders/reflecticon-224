#!/usr/bin/env node
// Precompute a web catalog (page/catalogs/HASH.json) for one firmware set,
// in Node, one worker per program in parallel. Uses the same operator code
// as the page (larc.js) and the same WASM build (lexicon224x.js).
//
//   node page/precompute.mjs ROM_SET_DIRECTORY [--jobs 8] [--only 5.1,4.1] [--names LARC_CATALOG]
//
// 224XL sets are operated through the LARC (larc.js), 224X sets through the
// front panel (panel.js), original-224 sets (ROM1-ROM4) through its own front
// panel operator (panel224.js). --only runs a trial on a few programs (B.P, or a
// panel program's identity in hex) and saves nothing. A catalog is only
// written if every program succeeded and it passes its problem check.
// --names (panel sets): name programs and parameters from a LARC catalog of
// the same program library, matched by displayed values (224XL v8.1A for v8.1).
import { createHash } from 'node:crypto';
import { readdirSync, readFileSync, writeFileSync, mkdirSync } from 'node:fs';
import { join, dirname } from 'node:path';
import { fileURLToPath } from 'node:url';
import { Worker, isMainThread, parentPort, workerData } from 'node:worker_threads';
import { cpus } from 'node:os';
import createLexicon from './lexicon224x.js';
import { createMachine, createOperator, chipBase, modelOf, catalogProblems } from './larc.js';
import { createPanelOperator, nameFromLarc, panelCatalogProblems } from './panel.js';
import { createPanel224Operator, panel224CatalogProblems } from './panel224.js';

const here = dirname(fileURLToPath(import.meta.url));

function readChips(directory) {
  const chips = [];
  chips.model = modelOf(readdirSync(directory).filter((name) => chipBase(name) !== null));
  for (const name of readdirSync(directory)) {
    const base = chipBase(name);
    if (base !== null) chips.push({ base, bytes: new Uint8Array(readFileSync(join(directory, name))) });
  }
  chips.sort((a, b) => a.base - b.base);
  return chips;
}
// The page's key: first 8 bytes of SHA-256 over the chips in address order.
function romHash(chips) {
  const hash = createHash('sha256');
  for (const c of chips) hash.update(c.bytes);
  return hash.digest('hex').slice(0, 16);
}

async function bootedOperator(chips, model) {
  const Module = await createLexicon();
  const m = createMachine(Module, { soon: setImmediate });
  m.onFail = () => { throw new Error('the machine stopped'); };
  m.powerOn(chips, model);
  await m.sleep(16);                    // v8.21 has the LARC connected by about 15 s
  if (model === 1) return { m, op: createPanel224Operator(m) };
  return { m, op: m.api.larcConnected(m.handle) ? createOperator(m) : createPanelOperator(m) };
}

if (!isMainThread) {
  // A worker: one program.
  const { chips, model, entry } = workerData;
  const started = Date.now();
  try {
    const { m, op } = await bootedOperator(chips, model);
    const result = await op.describeProgramFully(entry);
    parentPort.postMessage({ result, seconds: (Date.now() - started) / 1000, machine: m.time });
  } catch (e) {
    parentPort.postMessage({ error: e.message });
  }
} else {
  const args = process.argv.slice(2);
  const option = (name, fallback) => { const i = args.indexOf(name); return i >= 0 ? args.splice(i, 2)[1] : fallback; };
  const jobs = +option('--jobs', Math.max(1, cpus().length - 2));
  const only = option('--only', null);
  const names = option('--names', null);
  const directory = args[0];
  if (!directory) { console.error('usage: node page/precompute.mjs ROM_SET_DIRECTORY [--jobs N] [--only B.P,...]'); process.exit(2); }
  const chips = readChips(directory), hash = romHash(chips);
  console.log(`firmware ${hash}: ${chips.length} chips; reading the program list…`);
  const started = Date.now();
  const { op } = await bootedOperator(chips, chips.model);
  const catalog = await op.scanCatalog();
  console.log(`${catalog.length} programs: ${catalog.map((e) => e.name).join(', ')}`);
  const panel = op.kind === 'panel';
  const key = (e) => panel ? e.identity.toString(16).padStart(2, '0') : `${e.bank}.${e.program}`;
  const wanted = only ? catalog.filter((e) => only.split(',').includes(key(e))) : catalog;

  const results = new Map(), failures = [];
  let next = 0;
  await new Promise((resolveAll) => {
    let running = 0;
    const launch = () => {
      while (running < jobs && next < wanted.length && !failures.length) {
        const entry = wanted[next++];
        running++;
        const worker = new Worker(fileURLToPath(import.meta.url), { workerData: { chips: [...chips], model: chips.model, entry } });
        worker.once('message', (message) => {
          if (message.error) {
            failures.push(`${entry.name}: ${message.error}`);
            console.log(`FAILED ${entry.name}: ${message.error}`);
          } else {
            results.set(key(entry), message.result);
            console.log(`ok ${entry.name} (${message.result.variations.length} variations, ` +
              `${message.result.pages.length} pages) in ${message.seconds.toFixed(0)} s`);
          }
          worker.terminate();
        });
        worker.once('error', (e) => { failures.push(`${entry.name}: worker crashed: ${e.message}`); console.log(`FAILED ${entry.name}: ${e.message}`); });
        worker.once('exit', () => { running--; if (running === 0 && (next >= wanted.length || failures.length)) resolveAll(); else launch(); });
      }
    };
    launch();
  });

  const out = { version: 1, rom: hash, made: new Date().toISOString(),
    remote: op.remote || (panel ? 'panel' : 'larc'), programs: wanted.filter((e) => results.has(key(e))).map((e) => results.get(key(e))) };
  if (panel && names) {
    out.names = { from: JSON.parse(readFileSync(names, 'utf8')).rom,
      matches: nameFromLarc(out.programs, JSON.parse(readFileSync(names, 'utf8'))) };
    for (const r of out.names.matches)
      console.log(`${r.buttons.padEnd(10)} ${String(r.name).padEnd(14)} ${r.same}/${r.total} values agree with ${r.larc}; ` +
        `next ${r.runnerUp}${r.differences.length ? '\n    ' + r.differences.join('\n    ') : ''}`);
  }
  const check = op.remote === 'panel224' ? panel224CatalogProblems : panel ? panelCatalogProblems : catalogProblems;
  const problems = [...failures, ...check(out, wanted.length)];
  if (panel && names) for (const r of out.names.matches) if (!r.name) problems.push(`${r.buttons}: no clear name (best ${r.larc} ${r.same}/${r.total})`);
  const minutes = ((Date.now() - started) / 60000).toFixed(1);
  if (problems.length) {
    const path = join(here, '..', 'build', `failed-${hash}.json`);
    writeFileSync(path, JSON.stringify({ ...out, problems }));
    console.log(`PRECOMPUTE FAILED after ${minutes} min (kept for inspection in ${path}):\n  ${problems.join('\n  ')}`);
    process.exit(1);
  }
  if (only) {
    const path = join(here, '..', 'build', `trial-${hash}.json`);
    writeFileSync(path, JSON.stringify(out));
    console.log(`TRIAL OK after ${minutes} min (not a catalog; written to ${path})`);
    process.exit(0);
  }
  mkdirSync(join(here, 'catalogs'), { recursive: true });
  const path = join(here, 'catalogs', `${hash}.json`);
  writeFileSync(path, JSON.stringify(out));
  console.log(`saved ${path}: ${out.programs.length} programs after ${minutes} min`);
}
