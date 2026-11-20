// Test sources, synthesized (no samples shipped): a drum loop, plucked notes, clicks, noise bursts.
// Each is a looping stereo AudioBuffer, cached per context.
const cache = new WeakMap();

export function makeSource(ctx, kind) {
  let c = cache.get(ctx);
  if (!c) cache.set(ctx, (c = {}));
  return (c[kind] ||= build(ctx, kind));
}

function build(ctx, kind) {
  const sr = ctx.sampleRate;
  let seed = 12345;
  const rnd = () => ((seed = (seed * 1664525 + 1013904223) >>> 0) / 2 ** 32) * 2 - 1;
  const secs = { drums: 4.8, pluck: 6.4, clicks: 2, noise: 3 }[kind] || 2;
  const n = Math.round(secs * sr);
  const L = new Float32Array(n), R = new Float32Array(n);
  const add = (at, fn, len, pan = 0) => {
    const i0 = Math.round(at * sr), m = Math.round(len * sr);
    for (let i = 0; i < m && i0 + i < n; i++) {
      const v = fn(i / sr);
      L[i0 + i] += v * (1 - pan);
      R[i0 + i] += v * (1 + pan);
    }
  };
  if (kind === 'drums') {   // 100 bpm, two bars
    const beat = 60 / 100, s16 = beat / 4;
    const kick = (t) => Math.sin(2 * Math.PI * (45 * t + 90 * (1 - Math.exp(-t * 25)) / 25)) * Math.exp(-t * 7) * 0.8;
    const snare = (t) => (rnd() * 0.6 + Math.sin(2 * Math.PI * 190 * t) * 0.4) * Math.exp(-t * 18) * 0.55;
    let hp = 0, prev = 0;
    const hat = (t) => { const x = rnd(); hp = 0.6 * (hp + x - prev); prev = x; return hp * Math.exp(-t * 60) * 0.25; };
    for (let bar = 0; bar < 2; bar++) {
      const b0 = bar * 4 * beat;
      for (const k of bar === 1 ? [0, 2.5, 3] : [0, 2.5]) add(b0 + k * beat, kick, 0.5);
      add(b0 + beat, snare, 0.35); add(b0 + 3 * beat, snare, 0.35);
      for (let h = 0; h < 16; h++) if (h % 2 === 0 || h === 15) add(b0 + h * s16, hat, 0.08, 0.3);
    }
  } else if (kind === 'pluck') {   // Karplus-Strong notes, a minor-ish figure
    const notes = [57, 64, 67, 72, 69, 64, 60, 62];
    notes.forEach((m, k) => {
      const f = 440 * 2 ** ((m - 69) / 12), p = Math.max(2, Math.round(sr / f));
      const line = new Float32Array(p).map(() => rnd() * 0.5);
      let idx = 0, last = 0;
      add(k * 0.8, () => {
        const v = line[idx];
        const nv = 0.996 * 0.5 * (v + last);
        last = v; line[idx] = nv; idx = (idx + 1) % p;
        return v;
      }, 0.75, k % 2 ? 0.25 : -0.25);
    });
  } else if (kind === 'clicks') {
    add(0.05, (t) => (t < 1 / sr ? 0.9 : 0), 0.001);
  } else if (kind === 'noise') {
    add(0.05, (t) => rnd() * 0.5 * Math.min(1, (0.25 - t) * 200), 0.25);
  }
  const buf = ctx.createBuffer(2, n, sr);
  buf.copyToChannel(L, 0);
  buf.copyToChannel(R, 1);
  return buf;
}
