// Firmware sets from whatever files the visitor gives the page: chip files,
// folders' worth of them, or .zip archives. Known chips are recognized by
// their content (firmware_sets.json lists their SHA-256, never their bytes),
// whatever they are named; files that match no known set are placed by
// their names (SBCn, NVSn, ROMn) as one set of their own.
import { chipBase, modelOf } from './larc.js';
import { readZip } from './zip.js';

let known = null;
async function knownSets() {
  if (!known) {
    try { known = await (await fetch('firmware_sets.json')).json(); } catch { known = []; }
  }
  return known;
}

async function sha256(bytes) {
  const digest = new Uint8Array(await crypto.subtle.digest('SHA-256', bytes));
  return Array.from(digest, (x) => x.toString(16).padStart(2, '0')).join('');
}

// Expand any .zip among `files` ([{name, bytes}]) into its contents.
export async function expandArchives(files) {
  const out = [];
  for (const file of files) {
    if (/\.zip$/i.test(file.name)) out.push(...await readZip(file.bytes.buffer.slice(file.bytes.byteOffset, file.bytes.byteOffset + file.bytes.length)));
    else out.push(file);
  }
  return out;
}

// The sets among `files` ([{name, bytes}]): [{name, model, chips: [{base, bytes}],
// unsupported?}], known complete sets first, then a by-name set of the rest.
export async function findSets(files) {
  const hashes = await Promise.all(files.map((file) => sha256(file.bytes)));
  const byHash = new Map(files.map((file, i) => [hashes[i], file]));
  const sets = [], used = new Set();       // (hashes: copies of a used chip are used too)
  for (const set of await knownSets()) {
    if (!set.chips.every((chip) => byHash.has(chip.sha256))) continue;
    const chips = set.chips.map((chip) => ({ base: chip.base, bytes: byHash.get(chip.sha256).bytes }));
    set.chips.forEach((chip) => used.add(chip.sha256));
    sets.push({ name: set.name, model: set.model, chips, unsupported: set.unsupported });
  }
  const rest = files.filter((file, i) => !used.has(hashes[i]) && chipBase(file.name) !== null);
  if (rest.length) {
    const chips = [], placed = new Set();
    for (const file of rest) {
      const base = chipBase(file.name);
      if (placed.has(base)) continue;                  // (a duplicate of a chip already placed)
      placed.add(base);
      chips.push({ base, bytes: file.bytes });
    }
    sets.push({ name: 'unrecognized chips (placed by name)', model: modelOf(rest.map((f) => f.name)), chips });
  }
  for (const set of sets) set.chips.sort((a, b) => a.base - b.base);
  return sets;
}

// The newest of `sets` that runs: the last in firmware_sets.json's order
// (oldest machine and version first), else the first supported one.
export async function newestSet(sets) {
  const order = (await knownSets()).map((set) => set.name);
  const runnable = sets.filter((set) => !set.unsupported);
  const known = runnable.filter((set) => order.includes(set.name));
  if (!known.length) return runnable[0] || null;
  return known.reduce((a, b) => (order.indexOf(b.name) > order.indexOf(a.name) ? b : a));
}

// What kind of machine a set is, for its list entry.
export function describeSet(set) {
  if (set.model === 1) return 'original 224, front panel';
  if (set.chips.filter((c) => c.base < 0x8000).length >= 3) return '224XL, LARC remote';
  return '224X, front panel';
}

// A set's identity: the first 8 bytes of the SHA-256 of its chips in
// address order, as hex (the name of its catalog, page/catalogs/HASH.json).
export async function setHash(chips) {
  const all = new Uint8Array(chips.reduce((n, c) => n + c.bytes.length, 0));
  let offset = 0;
  for (const c of chips) { all.set(c.bytes, offset); offset += c.bytes.length; }
  return Array.from(new Uint8Array(await crypto.subtle.digest('SHA-256', all)).slice(0, 8),
    (x) => x.toString(16).padStart(2, '0')).join('');
}

// The development server's firmware (serve.py --roms): calls add(files)
// ([{name, bytes}]) once per set folder, in order; nothing when the page is
// served plainly.
export async function devServerFiles(add) {
  try {
    const response = await fetch('roms/index.json');
    if (response.ok) {
      const sets = await response.json();
      for (const [set, names] of Object.entries(sets)) {
        await add(await Promise.all(names.map(async (name) => ({ name, bytes: new Uint8Array(await
          (await fetch(`roms/${encodeURIComponent(set)}/${encodeURIComponent(name)}`)).arrayBuffer()) }))));
      }
    }
  } catch {}
}
