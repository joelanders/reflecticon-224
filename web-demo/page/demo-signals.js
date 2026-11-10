// demo-signals.js: the generated input sounds every emulator page offers, the
// same file copied into each page's repo so the drum loop and the plucks sound
// the same everywhere. Pure functions of the sample rate, each returning a mono
// Float32Array meant to be looped; deterministic (seeded noise), so a Node test
// can feed an engine the same material the browser plays.
//
// The riff is not generated: it is sounds/aphex-twin-tha-riff.flac (a dry
// SH-101 recording, thanks to SOLON Music), fetched and decoded by the page.

export const RIFF_URL = 'sounds/aphex-twin-tha-riff.flac';
export const RIFF_CREDIT_HTML = 'Thank you to <a href="https://www.solonmusic.com/" target="_blank" rel="noopener">SOLON Music</a> for the dry recording. Inspired by his <a href="https://www.youtube.com/watch?v=5cbjUZhDyXs" target="_blank" rel="noopener">224 video</a> and his <a href="https://www.youtube.com/watch?v=X-hGwcU-Ckw" target="_blank" rel="noopener">224XL video</a>.';

// The shared list, in order: value -> label. Every page shows these, in this
// order, with the riff selected by default; page-specific extras go after
// 'silence' and before 'mic'.
export const SOURCES = {
  riff: 'Aphex Twin riff on an SH-101 (loops)',
  drums: 'drum loop',
  pluck: 'guitar plucks',
  clicks: 'clicks',
  impulse: 'impulse (for measuring)',
  noise: 'noise bursts',
  sine: 'sine 440 Hz',
  silence: 'silence',
  mic: 'microphone / line in',
  file: 'your own audio file…',
};

function rng(seed) {
  let s = seed >>> 0;
  return () => { s = (Math.imul(s, 1664525) + 1013904223) >>> 0; return s / 2147483648 - 1; };
}

// Two bars at 100 bpm: kick on 1 and 3 (and the and of 3 in the second bar),
// snare on 2 and 4, eighth-note hats. 4.8 s, peaking near 0.6.
export function drums(sr) {
  const beat = 60 / 100, len = Math.round(beat * 8 * sr);
  const b = new Float32Array(len);
  const rnd = rng(1);
  const add = (t0, fn, dur) => {
    const i0 = Math.round(t0 * sr), n = Math.round(dur * sr);
    for (let i = 0; i < n && i0 + i < len; i++) b[i0 + i] += fn(i / sr);
  };
  const kick = (a) => (x) => a * Math.sin(2 * Math.PI * (50 * x + 120 * (1 - Math.exp(-x * 30)) / 30)) * Math.exp(-x * 9);
  for (let k = 0; k < 8; k++) {
    const t = k * beat;
    if (k % 2 === 0) add(t, kick(0.9), 0.5);
    if (k === 5) add(t + beat / 2, kick(0.6), 0.4);
    if (k % 2 === 1) add(t, (x) => (0.5 * rnd() + 0.3 * Math.sin(2 * Math.PI * 190 * x)) * Math.exp(-x * 18), 0.35);
    for (let h = 0; h < 2; h++) add(t + h * beat / 2, (x) => 0.12 * rnd() * Math.exp(-x * 60), 0.08);
  }
  for (let i = 0; i < len; i++) b[i] *= 0.6;   // peaks near 0.6, like the plucks and clicks
  return b;
}

// Karplus-Strong plucks with a short pick click: a little open-chord phrase,
// then a strum. 4 s.
export function pluck(sr) {
  const b = new Float32Array(sr * 4);
  const rnd = rng(7);
  const hz = (m) => 440 * 2 ** ((m - 69) / 12);
  const notes = [[0, 40], [0.3, 47], [0.6, 52], [0.9, 55], [1.2, 59], [1.5, 55],
    [2.2, 40], [2.225, 47], [2.25, 52], [2.275, 55], [2.3, 59], [2.325, 64]];
  for (const [t0, m] of notes) {
    const len = Math.max(2, Math.round(sr / hz(m)));
    const ring = new Float32Array(len);
    for (let i = 0; i < len; i++) ring[i] = rnd();
    const start = Math.round(t0 * sr);
    const fb = 0.5 * (1 - 1.2 / (hz(m) * 2.2));   // ~2 s ring-down whatever the pitch
    for (let j = 0, i = start; i < b.length && j < sr * 2; i++, j++) {
      const k = j % len;
      const x = ring[k];
      ring[k] = fb * (x + ring[(k + 1) % len]);
      b[i] += 0.16 * x + (j < 24 ? 0.25 * Math.exp(-j / 4) * (j % 2 ? -1 : 1) : 0);
    }
  }
  return b;
}

// Two short clicks a second.
export function clicks(sr) {
  const b = new Float32Array(sr);
  for (const s of [0, sr >> 1]) for (let i = 0; i < 48; i++) b[s + i] = 0.6 * Math.exp(-i / 6) * (i % 2 ? -1 : 1);
  return b;
}

// One sample at 0.5 every 3 s.
export function impulse(sr) {
  const b = new Float32Array(sr * 3);
  b[0] = 0.5;
  return b;
}

// A 200 ms noise burst every 2 s.
export function noise(sr) {
  const b = new Float32Array(sr * 2);
  const rnd = rng(3);
  const n = Math.round(sr * 0.2);
  for (let i = 0; i < n; i++) b[i] = 0.3 * Math.min(1, i / 48, (n - i) / 480) * rnd();
  return b;
}

// 440 Hz; one second holds a whole number of cycles at any integer rate, so it loops cleanly.
export function sine(sr) {
  const b = new Float32Array(sr);
  for (let i = 0; i < sr; i++) b[i] = 0.3 * Math.sin(2 * Math.PI * 440 * i / sr);
  return b;
}

export const GENERATORS = { drums, pluck, clicks, impulse, noise, sine };
