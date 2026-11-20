#!/usr/bin/env python3
"""Tone tests through an audio path renderer: spurs, harmonic vs inharmonic.

  analyze.py RENDERER [ROM_DIR] > results/NAME.txt

RENDERER takes (ROM_DIR, IN.f32, OUT.f32). Each test is a 2.5 s tone; the
last 65536 samples of the output (after MAX DELAY's 0.5 s) are analysed with
a 4-term Blackman-Harris window. Reported per test: output level, the
largest inharmonic spur (not within 20 Hz of a harmonic of the tone) and the
total inharmonic and harmonic power relative to the tone, 20 Hz-20 kHz.
"""
import subprocess, sys
from pathlib import Path
import numpy as np

FS = 48000
N = 65536
TESTS = [(1000, 0.5), (5000, 0.5), (7000, 0.5), (10000, 0.5), (14000, 0.5), (1000, 0.002)]
import os
if os.environ.get("LEVEL"):
    TESTS = [(f, float(os.environ["LEVEL"])) for f in (1000, 5000, 7000, 10000, 12000, 14000, 15000)]
# TONES="f:amp,f:amp,...": any list (the Model 224 renders, tools/render_io_224.cpp).
if os.environ.get("TONES"):
    TESTS = [(int(t.split(":")[0]), float(t.split(":")[1])) for t in os.environ["TONES"].split(",")]


def spectrum(y):
    w = np.blackman(N) if False else (0.35875 - 0.48829*np.cos(2*np.pi*np.arange(N)/N)
                                      + 0.14128*np.cos(4*np.pi*np.arange(N)/N) - 0.01168*np.cos(6*np.pi*np.arange(N)/N))
    Y = np.fft.rfft(y[-N:] * w)
    p = np.abs(Y)**2
    f = np.fft.rfftfreq(N, 1/FS)
    return f, p


def main():
    renderer, rom = sys.argv[1], sys.argv[2] if len(sys.argv) > 2 else "../../firmware/224X v8_1"
    build = Path("build"); build.mkdir(exist_ok=True)
    print(f"renderer: {renderer}")
    print(f"{'tone':>8} {'amp':>7} {'out dBFS':>9} {'worst inharmonic spur':>28} {'inharm dBc':>11} {'harm dBc':>9}")
    for f0, amp in TESTS:
        t = np.arange(int(2.5*FS)) / FS
        (amp*np.sin(2*np.pi*f0*t)).astype(np.float32).tofile(build/"in.f32")
        subprocess.run([renderer, rom, str(build/"in.f32"), str(build/"out.f32")], check=True,
                       stderr=subprocess.DEVNULL)
        y = np.fromfile(build/"out.f32", np.float32).astype(np.float64)
        f, p = spectrum(y)
        band = (f > 20) & (f < 20000)
        bw = 6  # bins either side of a line (window main lobe)
        k0 = int(round(f0 / (FS/N)))
        tone = p[k0-bw:k0+bw+1].sum()
        harm_mask = np.zeros_like(band)
        for h in range(1, 40):
            kh = int(round(h*f0 / (FS/N)))
            if kh < len(p):
                harm_mask[max(0, kh-bw):kh+bw+1] = True
        inh = band & ~harm_mask
        harm = band & harm_mask
        harm[k0-bw:k0+bw+1] = False
        # worst inharmonic spur: local peak power summed over the main lobe
        pk = np.argmax(np.where(inh, p, 0))
        spur = p[max(0, pk-bw):pk+bw+1].sum()
        lvl = 10*np.log10(tone / (N*0.35875)**2 * 4) if tone > 0 else -999
        def db(x): return 10*np.log10(x/tone) if x > 0 and tone > 0 else -999
        print(f"{f0:8d} {amp:7.3f} {lvl:9.1f} {f[pk]:10.0f} Hz {db(spur):8.1f} dBc {db(p[inh].sum()):10.1f} {db(p[harm].sum()):9.1f}")


if __name__ == "__main__":
    main()
