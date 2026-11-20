// The web demo's own recognition (web/firmware.js findSets + expandArchives,
// app.js romHash) over folders and zips, printed in the same canonical lines
// as the C++ harnesses, so the two can be diffed.
//
//   node tests/roms/web_reference.mjs LABEL=PATH [LABEL=PATH ...]
//
// A PATH is a folder (walked recursively, files sorted by full path, as
// RomLibrary::gather), a .zip, or a single file.
import { readFileSync, readdirSync, statSync } from 'node:fs';
import { join, dirname, basename } from 'node:path';
import { fileURLToPath } from 'node:url';
import { createHash } from 'node:crypto';

const web = join(dirname(fileURLToPath(import.meta.url)), '..', '..', '..', 'web-demo', 'page');
const known = JSON.parse(readFileSync(join(web, 'firmware_sets.json'), 'utf8'));
globalThis.fetch = async () => ({ ok: true, json: async () => known });
const { expandArchives, findSets } = await import(join(web, 'firmware.js'));

function walk(path, out) {
  let st;
  try { st = statSync(path); } catch { return; }          // (a broken symlink)
  if (st.isDirectory()) {
    for (const name of readdirSync(path)) walk(join(path, name), out);
  } else if (st.isFile()) {
    out.push(path);
  }
}

for (const arg of process.argv.slice(2)) {
  const at = arg.indexOf('=');
  const label = arg.slice(0, at), path = arg.slice(at + 1);
  const paths = [];
  walk(path, paths);
  paths.sort();
  const files = paths.map((p) => ({ name: basename(p), bytes: new Uint8Array(readFileSync(p)) }));
  const sets = await findSets(await expandArchives(files));
  if (!sets.length) console.log(`${label}\t(none)`);
  for (const set of sets) {
    const hash = createHash('sha256');
    for (const c of set.chips) hash.update(c.bytes);
    const bases = set.chips.map((c) => c.base.toString(16)).join(',');
    const knownSet = !set.name.startsWith('unrecognized');
    console.log(`${label}\t${set.name}\tmodel=${set.model}\tknown=${knownSet ? 1 : 0}\tsupported=${set.unsupported ? 0 : 1}` +
      `\thash=${hash.digest('hex').slice(0, 16)}\tbases=${bases}`);
  }
}
