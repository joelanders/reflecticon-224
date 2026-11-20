#!/usr/bin/env node
// The other half of the byte-exact gate: the web build itself (the WASM in
// ../../../web-demo/page/) on byte_exact.cpp's ROM, input and
// controls, compared bit for bit with the native reference that
// `byte_exact --dump FILE` writes. Together: web build == native reference
// == plugin.
//
//   node tests/byte_exact/compare_wasm.mjs ROM_DIR TIMELINE.events REFERENCE.f32 [TAIL_SECONDS]
//
// Same alignment as byte_exact.cpp: frames from power-on, silence until
// BOOT_FRAMES, each control given before its frame, the four channels
// A,B,C,D of frames BOOT_FRAMES.. compared.
import { readdirSync, readFileSync } from 'node:fs';
import { join, dirname } from 'node:path';
import { fileURLToPath } from 'node:url';

const rows = join(dirname(fileURLToPath(import.meta.url)), '../../../web-demo');
const { default: createLexicon } = await import(join(rows, 'page/lexicon224x.js'));
const { chipBase, modelOf } = await import(join(rows, 'page/larc.js'));

const BOOT_FRAMES = 16 * 48000;   // PluginProcessor::boot_frames
const [directory, timelinePath, referencePath, tailArg] = process.argv.slice(2);
if (!referencePath) {
  console.error('usage: node compare_wasm.mjs ROM_DIR TIMELINE.events REFERENCE.f32 [TAIL_SECONDS]');
  process.exit(2);
}
const tail = Number(tailArg ?? 5);

const events = [];
let end = 0;
for (const line of readFileSync(timelinePath, 'utf8').split('\n')) {
  const parts = line.trim().split(/\s+/);
  if (parts[0] === '') {
    continue;
  }
  if (parts[0] === 'end') {
    end = Number(parts[1]);
    continue;
  }
  if (parts[1] === 'mark') {
    continue;
  }
  events.push({ frame: Number(parts[0]), kind: parts[1], a: Number(parts[2]), b: Number(parts[3]) });
}
events.sort((x, y) => x.frame - y.frame);   // stable, as byte_exact.cpp's
const total = end + Math.round(tail * 48000);

// byte_exact.cpp input_at, in BigInt for the 64-bit mix.
const M64 = (1n << 64n) - 1n;
function inputAt(frame, channel) {
  if (frame < BOOT_FRAMES) {
    return 0;
  }
  const since = frame - BOOT_FRAMES;
  if (since % 96000 === 1000) {
    return Math.fround(0.6);
  }
  let x = (BigInt(frame) * 2n + BigInt(channel) + 0x9E3779B97F4A7C15n) & M64;
  x ^= x >> 33n;
  x = (x * 0xff51afd7ed558ccdn) & M64;
  x ^= x >> 33n;
  x = (x * 0xc4ceb9fe1a85ec53n) & M64;
  x ^= x >> 33n;
  return Math.fround((Number(x >> 40n) / 16777216 * 2 - 1) * 0.25);
}

const Module = await createLexicon();
const c = (name, ret, args) => Module.cwrap(name, ret, args);
const n = 'number';
const api = {
  create: c('lex_create', n, [n]), load: c('lex_load', null, [n, n, n, n]),
  render: c('lex_render', n, [n, n, n, n, n]), button: c('lex_button', null, [n, n, n]),
  pot: c('lex_pot', null, [n, n, n]), key: c('lex_key', n, [n, n, n]), fader: c('lex_fader', n, [n, n, n]),
  poke: c('lex_poke', null, [n, n, n]),
};
const names = readdirSync(directory).filter((name) => chipBase(name) !== null);
const handle = api.create(modelOf(names));
for (const name of names) {
  const bytes = new Uint8Array(readFileSync(join(directory, name)));
  const ptr = Module._malloc(bytes.length);
  Module.HEAPU8.set(bytes, ptr);
  api.load(handle, ptr, bytes.length, chipBase(name));
  Module._free(ptr);
}

const CHUNK = 4800;
const inL = Module._malloc(CHUNK * 4), inR = Module._malloc(CHUNK * 4), out = Module._malloc(CHUNK * 16);
const reference = new Float32Array(readFileSync(referencePath).buffer.slice(0));
const expected = (total - BOOT_FRAMES) * 4;
if (reference.length !== expected) {
  console.error(`reference has ${reference.length} samples, expected ${expected} (same TAIL_SECONDS?)`);
  process.exit(1);
}
const refBits = new Uint32Array(reference.buffer);
let frame = 0, next = 0, differing = 0, first = -1;
while (frame < total) {
  while (next < events.length && events[next].frame <= frame) {
    const e = events[next++];
    api[e.kind](handle, e.a, e.b);
  }
  let count = Math.min(CHUNK, total - frame);
  if (next < events.length) {
    count = Math.min(count, events[next].frame - frame);
  }
  const l = new Float32Array(Module.HEAPF32.buffer, inL, count), r = new Float32Array(Module.HEAPF32.buffer, inR, count);
  for (let i = 0; i < count; i++) {
    l[i] = inputAt(frame + i, 0);
    r[i] = inputAt(frame + i, 1);
  }
  if (api.render(handle, inL, inR, out, count)) {
    console.error('the machine stopped');
    process.exit(1);
  }
  const bits = new Uint32Array(Module.HEAPU8.buffer, out, count * 4);
  for (let i = 0; i < count; i++) {
    const f = frame + i;
    if (f < BOOT_FRAMES) {
      continue;
    }
    for (let ch = 0; ch < 4; ch++) {
      if (bits[i * 4 + ch] !== refBits[(f - BOOT_FRAMES) * 4 + ch]) {
        differing++;
        if (first < 0) {
          first = f;
        }
      }
    }
  }
  frame += count;
}
const compared = total - BOOT_FRAMES;
if (differing === 0) {
  console.log(`compare_wasm: PASS: ${compared} frames x 4 channels bit-identical (frames ${BOOT_FRAMES}..${total - 1})`);
} else {
  console.log(`compare_wasm: FAIL: ${differing} of ${compared * 4} samples differ, first at frame ${first}`);
  process.exit(1);
}
