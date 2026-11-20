#!/usr/bin/env node
// Precompute a catalog for an original-224 firmware set the web demo has no
// catalog for, into juce-plugin/catalogs-extra/HASH.json.
//
// Adapted from ../../web-demo/page/precompute.mjs (same boot, same
// worker-per-program scheme, same problem check, same JSON), which writes
// only into its own page/catalogs/. The
// differences: the output directory; 224 sets only; and the operator is
// chosen by the firmware's RAM layout (tools/panel224_layouts.mjs): v3.2
// needs its own, every other 224 set is driven by the web's panel224.js.
//
//   node tools/precompute_extra.mjs ROM_SET_DIRECTORY [--jobs N] [--only 01,02]
//        [--copy]      drive a v4 set with the layout copy instead of panel224.js
//                      (to check the copy against a shipped catalog)
//        [--out DIR]   where to write (default ../catalogs-extra)
// --only runs a trial on some programs (identity in hex) and writes it to
// build-g/trial-HASH.json. A failed run is kept in build-g/failed-HASH.json.
import { createHash } from 'node:crypto';
import { readdirSync, readFileSync, writeFileSync, mkdirSync } from 'node:fs';
import { join, dirname, resolve } from 'node:path';
import { fileURLToPath } from 'node:url';
import { Worker, isMainThread, parentPort, workerData } from 'node:worker_threads';
import { cpus } from 'node:os';
import createLexicon from '../../web-demo/page/lexicon224x.js';
import { createMachine, chipBase, modelOf } from '../../web-demo/page/larc.js';
import { createPanel224Operator, panel224CatalogProblems } from '../../web-demo/page/panel224.js';
import { createPanel224OperatorWith, LAYOUT_V4, LAYOUT_V32 } from './panel224_layouts.mjs';

const here = dirname(fileURLToPath(import.meta.url));
const plugin = join(here, '..');

// Sets whose RAM layout is not v4.x, by catalog hash.
const LAYOUT_BY_HASH = { df8e8a9842ccb188: LAYOUT_V32 };

function readChips(directory) {
  const chips = [];
  chips.model = modelOf(readdirSync(directory).filter((name) => chipBase(name) !== null));
  for (const name of readdirSync(directory)) {
    const base = chipBase(name);
    if (base !== null) {
      chips.push({ base, bytes: new Uint8Array(readFileSync(join(directory, name))) });
    }
  }
  chips.sort((a, b) => a.base - b.base);
  return chips;
}
// The page's key: first 8 bytes of SHA-256 over the chips in address order.
function romHash(chips) {
  const hash = createHash('sha256');
  for (const c of chips) {
    hash.update(c.bytes);
  }
  return hash.digest('hex').slice(0, 16);
}

async function bootedOperator(chips, model, layoutName, copy) {
  const Module = await createLexicon();
  const m = createMachine(Module, { soon: setImmediate });
  m.onFail = () => { throw new Error('the machine stopped'); };
  m.powerOn(chips, model);
  await m.sleep(16);
  if (layoutName === LAYOUT_V32.name) {
    return { m, op: createPanel224OperatorWith(m, LAYOUT_V32) };
  }
  if (copy) {
    return { m, op: createPanel224OperatorWith(m, LAYOUT_V4) };
  }
  return { m, op: createPanel224Operator(m) };
}

if (!isMainThread) {
  const { chips, model, entry, layoutName, copy } = workerData;
  const started = Date.now();
  try {
    const { m, op } = await bootedOperator(chips, model, layoutName, copy);
    const result = await op.describeProgramFully(entry);
    parentPort.postMessage({ result, seconds: (Date.now() - started) / 1000, machine: m.time });
  } catch (e) {
    parentPort.postMessage({ error: e.message });
  }
} else {
  const args = process.argv.slice(2);
  const option = (name, fallback) => {
    const i = args.indexOf(name);
    if (i < 0) {
      return fallback;
    }
    return args.splice(i, 2)[1];
  };
  const flag = (name) => {
    const i = args.indexOf(name);
    if (i < 0) {
      return false;
    }
    args.splice(i, 1);
    return true;
  };
  const jobs = +option('--jobs', Math.max(1, cpus().length - 2));
  const only = option('--only', null);
  const outDir = resolve(option('--out', join(plugin, 'catalogs-extra')));
  const copy = flag('--copy');
  const directory = args[0];
  if (!directory) {
    console.error('usage: node tools/precompute_extra.mjs ROM_SET_DIRECTORY [--jobs N] [--only 01,...] [--copy] [--out DIR]');
    process.exit(2);
  }
  const chips = readChips(directory), hash = romHash(chips);
  if (chips.model !== 1) {
    console.error('not an original-224 set (ROM1-ROM4); use the web precompute');
    process.exit(2);
  }
  const layout = LAYOUT_BY_HASH[hash] || LAYOUT_V4;
  let driver = 'web panel224.js';
  if (layout !== LAYOUT_V4) {
    driver = `layout ${layout.name}`;
  } else if (copy) {
    driver = 'layout v4 (copy)';
  }
  console.log(`firmware ${hash}: ${chips.length} chips, operator: ${driver}; reading the program list…`);
  const started = Date.now();
  const { op } = await bootedOperator(chips, chips.model, layout.name, copy);
  const catalog = await op.scanCatalog();
  console.log(`${catalog.length} programs: ${catalog.map((e) => e.name).join(', ')}`);
  const key = (e) => e.identity.toString(16).padStart(2, '0');
  let wanted = catalog;
  if (only) {
    wanted = catalog.filter((e) => only.split(',').includes(key(e)));
  }

  const results = new Map(), failures = [];
  let next = 0;
  await new Promise((resolveAll) => {
    let running = 0;
    const launch = () => {
      while (running < jobs && next < wanted.length && !failures.length) {
        const entry = wanted[next++];
        running++;
        const worker = new Worker(fileURLToPath(import.meta.url),
          { workerData: { chips: [...chips], model: chips.model, entry, layoutName: layout.name, copy } });
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
        worker.once('error', (e) => {
          failures.push(`${entry.name}: worker crashed: ${e.message}`);
          console.log(`FAILED ${entry.name}: ${e.message}`);
        });
        worker.once('exit', () => {
          running--;
          if (running === 0 && (next >= wanted.length || failures.length)) {
            resolveAll();
          } else {
            launch();
          }
        });
      }
    };
    launch();
  });

  const out = { version: 1, rom: hash, made: new Date().toISOString(), remote: op.remote,
    programs: wanted.filter((e) => results.has(key(e))).map((e) => results.get(key(e))) };
  if (layout !== LAYOUT_V4) {
    out.layout = layout.name;
  }
  const problems = [...failures, ...panel224CatalogProblems(out, wanted.length)];
  const minutes = ((Date.now() - started) / 60000).toFixed(1);
  const build = join(plugin, 'build-g');
  mkdirSync(build, { recursive: true });
  if (problems.length) {
    const path = join(build, `failed-${hash}.json`);
    writeFileSync(path, JSON.stringify({ ...out, problems }));
    console.log(`PRECOMPUTE FAILED after ${minutes} min (kept for inspection in ${path}):\n  ${problems.join('\n  ')}`);
    process.exit(1);
  }
  if (only) {
    const path = join(build, `trial-${hash}.json`);
    writeFileSync(path, JSON.stringify(out));
    console.log(`TRIAL OK after ${minutes} min (not a catalog; written to ${path})`);
    process.exit(0);
  }
  mkdirSync(outDir, { recursive: true });
  const path = join(outDir, `${hash}.json`);
  writeFileSync(path, JSON.stringify(out));
  console.log(`saved ${path}: ${out.programs.length} programs after ${minutes} min`);
}
