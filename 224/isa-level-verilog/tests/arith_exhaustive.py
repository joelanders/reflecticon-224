#!/usr/bin/env python3
"""Exhaustive arithmetic check: one row's multiply-accumulate, three ways.

An independent Python spec, written from README.md ("One row", "The
instruction word") and not from either implementation, is compared bit for
bit with the C++ (isa-level-cpp/lexicon224x.hpp) and the SystemVerilog
(lexicon224x.sv, dsp.sv) arithmetic:

1. Exhaustive: every 16-bit register value x every coefficient (0..63) x
   negative in {0, 1}, from each of ~27 prior ACC values (one of them a ZERO
   row). Per case: the new ACC, the SAT flag of each of the three adds, and
   RR = ACC[18:3] as the next XFER would save it. C++ runs three routes
   (helpers composed as execute() uses them; the ARU pipeline via aruck();
   the whole Machine via step_row() on a WCS program), SystemVerilog runs the
   package functions.
2. Keep-shifting: x = previous x / 64 for every previous x in [-2^18, 2^18)
   (the operand's reachable range), x every coefficient and sign, from 4 priors.
3. Chains: random programs of multiply-accumulate chains (2-6 rows, random
   coefficients, signs, registers, ZERO/XFER positions, keep-shifting, RR and
   memory round trips) through the C++ Machine (load_wcs + step_row) and the
   SystemVerilog machine (dsp.sv), per-row state checked against the spec's
   row machine, which covers the order inside a row (register write before the
   operand read, XFER before ZERO before the product).

Comparisons are by checksum per block (a weighted sum mod 2^64 of one word
per case); on a mismatch the block is dumped from the implementation and the
first differing cases are printed with all inputs.

  python3 tests/arith_exhaustive.py                     # everything (~1-2 min)
  python3 tests/arith_exhaustive.py --mutate trunc_toward_zero   # must FAIL
  python3 tests/arith_exhaustive.py --mutate all        # every negative control must be caught

Ambiguities in the prose and how the spec resolves them are listed in AMBIGUITIES.
"""
from __future__ import annotations

import argparse
import os
import random
import struct
import subprocess
import sys
import time
from concurrent.futures import ThreadPoolExecutor
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT))
import run  # noqa: E402  (verilator())

try:
    import numpy as np
except ImportError:  # pragma: no cover
    sys.exit("numpy is required (python3.12 -m pip install numpy)")

BUILD = ROOT / "build" / "arith"

AMBIGUITIES = """
Where the README prose leaves room, and what this spec assumes:
 A1 "x/2 ... x/32, each term truncated": is each shifted multiplicand
    truncated (c1*floor(x/16) + c0*floor(x/32)), or each two-bit term as a whole
    (floor((2c1+c0)*x/32))? The sentence says the TERMS are truncated; the
    table says "x/16*c1 + x/32*c0". Chosen: every x/2^k is floor(x/2^k) (an
    arithmetic shift, as the task statement says), then c-bit multiples are
    summed exactly. Mutant 'term_truncated_whole' is the other reading and is
    caught, so the implementations do pick one.
 A2 "truncated": toward -inf (arithmetic shift), not toward zero (mutant
    'trunc_toward_zero').
 A3 x/8 = floor(x/8) directly; composing floor(floor(x/4)/2) is the same.
 A4 "saturated separately": each of the three adds clamps to
    [-2^18, 2^18-1] before the next; SAT for an add = the unclamped sum lies
    outside that range (a sum exactly at a rail is not SAT).
 A5 "negative subtracts": ACC - term, per term, same clamp (not one's
    complement, not negating the finished product).
 A6 RR := ACC[18:3] = floor(ACC/8) as 16 bits; ACC is always inside the
    19-bit range, so no clipping is needed.
 A7 "keep-shifting: x = previous x / 64": the previous ROW's x (loaded or
    itself keep-shifted), floor division; only on OPER rows (bit 4 is an OPER
    field). The very first row's "previous x" is 0 (power-on convention).
 A8 x = R[RA]*8 reads R[RA] as a signed 16-bit value.
 A9 Memory: the address is cpc - offset with the field holding OFST/; with no
    RESET in these programs cpc never moves, so the spec keys memory by the
    field alone (injective). Initial memory, registers, RR, x: 0; ACC: -1
    (README "Limits"). NOP and OPER source 0 put 0 on the bus.
 A10 The README defines no SAT output for the machine; the C++ Machine
    reports the three adds of the PREVIOUS row's multiply after each row
    (its pipeline), so the chain check compares C++ SAT one row late. The SV
    package has no SAT output; the SV harness derives it as "accumulate()
    returned something other than the plain 20-bit sum".
"""

ACC_MAX = (1 << 18) - 1
ACC_MIN = -(1 << 18)

# Prior ACC values. "Z" is a ZERO row (ACC cleared before this row's product).
PRIORS = ["Z", -1, 0, 1, ACC_MAX, ACC_MIN, ACC_MAX - 1, ACC_MIN + 1, ACC_MAX - 7, ACC_MIN + 7,
          ACC_MAX - 8, ACC_MIN + 8, 1 << 17, (1 << 17) - 1, -(1 << 17), -(1 << 17) - 1,
          (1 << 17) + 1, -(1 << 17) + 1, 7, 8, -8, -7, 3, -4, 100003, -123457, 196608, -196608]
KEEP_PRIORS = ["Z", -1, ACC_MAX, ACC_MIN]

EXHAUSTIVE_MUTANTS = {
    "trunc_toward_zero": "x/2^k truncates toward zero instead of toward -inf",
    "saturate_at_end": "the three terms are summed unclamped and saturated once at the end",
    "wrap_no_saturation": "ACC wraps at 20 bits instead of saturating",
    "term_truncated_whole": "each two-bit term is truncated as a whole: floor((2hi+lo)*x/2^(2k+1))",
    "single_truncation": "one product floor(x*c/32), one saturated add",
    "negate_ones_complement": "negative adds ~term (one's complement, no carry-in)",
    "rr_rounds": "RR = round(ACC/8) instead of ACC[18:3]",
    "sat_flag_at_rail": "SAT also when a sum lands exactly on a rail",
}
CHAIN_MUTANTS = {
    "rr_before_last_product": "XFER saves the sum before the previous row's last term (x/16*c1 + x/32*c0)",
    "xfer_after_product": "XFER saves the sum including this row's product",
    "zero_before_xfer": "ZERO clears before XFER reads",
    "operand_before_register_write": "the multiplicand reads R[RA] before this row's register write",
    "keep_shift_by_16": "keep-shifting divides the previous x by 16",
    "keep_from_register": "keep-shifting divides R[RA]*8 by 64 (not the previous x)",
}
MUTANTS = {**EXHAUSTIVE_MUTANTS, **CHAIN_MUTANTS}
MUTATION = None
STATS = {}  # coverage counts from the spec, for the summary


# ---------------------------------------------------------------------------
# THE SPEC, from the README prose.
# ---------------------------------------------------------------------------
def divide(x, k: int):
    """x / 2^k, truncated (A2): an arithmetic shift. Works on ints and arrays."""
    if MUTATION == "trunc_toward_zero":
        if isinstance(x, np.ndarray):
            return np.where(x < 0, -((-x) >> k), x >> k)
        return -((-x) >> k) if x < 0 else x >> k
    return x >> k


def term(x, c: int, k: int):
    """Term k (0, 1, 2) of x*c/32: x/4^k * c(5-2k) + x/(2*4^k) * c(4-2k) (A1)."""
    high, low = c >> (5 - 2 * k) & 1, c >> (4 - 2 * k) & 1
    if MUTATION == "term_truncated_whole":
        return ((2 * high + low) * x) >> (2 * k + 1)
    return high * divide(x, 2 * k) + low * divide(x, 2 * k + 1)


def row_product(acc, x, c: int, negative: bool):
    """ACC +/-= x*c/32 in three truncated, separately saturated terms.
    acc and x are ints or broadcastable int64 arrays. Returns (acc, sat), sat
    bit k = add k saturated (A4)."""
    vector = isinstance(acc, np.ndarray) or isinstance(x, np.ndarray)
    clip = (lambda v: np.clip(v, ACC_MIN, ACC_MAX)) if vector else (lambda v: max(ACC_MIN, min(ACC_MAX, v)))
    outside = (lambda v: ((v > ACC_MAX) | (v < ACC_MIN)).astype(np.int64)) if vector else \
        (lambda v: int(v > ACC_MAX or v < ACC_MIN))
    sign = -1 if negative else 1
    if MUTATION == "single_truncation":
        total = acc + sign * ((x * c) >> 5)
        return clip(total), outside(total)
    if MUTATION == "saturate_at_end":
        total = acc
        for k in range(3):
            total = total + sign * term(x, c, k)
        return clip(total), outside(total) << 2
    sat = 0
    for k in range(3):
        t = term(x, c, k)
        if negative and MUTATION == "negate_ones_complement":
            total = acc + (-t - 1)
        else:
            total = acc + sign * t
        if MUTATION == "wrap_no_saturation":
            acc = ((total + (1 << 19)) & ((1 << 20) - 1)) - (1 << 19)
            continue
        if MUTATION == "sat_flag_at_rail":
            flag = ((total >= ACC_MAX) | (total <= ACC_MIN)).astype(np.int64) if vector else \
                int(total >= ACC_MAX or total <= ACC_MIN)
        else:
            flag = outside(total)
        sat = sat | (flag << k)
        acc = clip(total)
    return acc, sat


def rr_of(acc):
    """RR := ACC[18:3] (A6), as a 16-bit pattern."""
    if MUTATION == "rr_rounds":
        v = (acc + 4) >> 3
        v = np.clip(v, -32768, 32767) if isinstance(v, np.ndarray) else max(-32768, min(32767, v))
        return v & 0xFFFF
    return (acc >> 3) & 0xFFFF


def signed16(v: int) -> int:
    return v - 0x10000 if v & 0x8000 else v


class RowMachine:
    """README "One row", steps 2, 3, 5 and 6 (no converter, no RESET)."""

    def __init__(self):
        self.R = [0, 0, 0, 0]
        self.X = 0
        self.ACC = -1
        self.RR = 0
        self.xreg_out = 0
        self.memory = {}
        self.last_term = 0  # the previous row's last term, signed (for one mutant)

    def row(self, word: int, xreg_in: int):
        c, zero, xfer, negative = word >> 26 & 63, word >> 25 & 1, word >> 24 & 1, word >> 23 & 1
        ra, wa, op, low = word >> 20 & 3, word >> 18 & 3, word >> 16 & 3, word & 0xFFFF
        oper = op == 1
        # 2. Bus.
        if oper:
            source = low >> 12 & 3
            bus = {0: 0, 1: self.RR, 2: xreg_in}.get(source, 0)  # source 3 (ADC) is not generated
        elif op == 2:
            bus = self.RR
        else:
            bus = 0
        # 3. Memory (A9).
        if op == 2:
            self.memory[low] = bus
        elif op == 3:
            bus = self.memory.get(low, 0)
        # 5. Register write, then the multiplicand read.
        before = list(self.R)
        self.R[wa] = bus
        if oper and low >> 6 & 1:
            self.xreg_out = bus
        # 6. Multiply-accumulate.
        keep = oper and low >> 4 & 1
        registers = before if MUTATION == "operand_before_register_write" else self.R
        if keep:
            if MUTATION == "keep_shift_by_16":
                x = self.X >> 4
            elif MUTATION == "keep_from_register":
                x = (signed16(registers[ra]) * 8) >> 6
            else:
                x = divide(self.X, 6)
        else:
            x = signed16(registers[ra]) * 8
        acc = self.ACC
        if MUTATION == "zero_before_xfer" and zero:
            acc = 0
        if xfer and MUTATION != "xfer_after_product":
            saved = acc
            if MUTATION == "rr_before_last_product":
                saved = max(ACC_MIN, min(ACC_MAX, acc - self.last_term))
            self.RR = rr_of(saved)
        if zero:
            acc = 0
        acc_before = acc
        acc, sat = row_product(acc, x, c, bool(negative))
        self.last_term = (-1 if negative else 1) * term(x, c, 2)
        if xfer and MUTATION == "xfer_after_product":
            self.RR = rr_of(acc)
        self.ACC = acc
        self.X = x
        return acc_before, sat


# ---------------------------------------------------------------------------
# Checksums, shared with the harnesses.
# ---------------------------------------------------------------------------
MASK64 = (1 << 64) - 1


def weights(n: int):
    i = np.arange(n, dtype=np.uint64)
    return (i * np.uint64(0x9E3779B97F4A7C15) + np.uint64(0x632BE59BD9B4E019)) | np.uint64(1)


def weight(i: int) -> int:
    return ((i * 0x9E3779B97F4A7C15 + 0x632BE59BD9B4E019) & MASK64) | 1


def words(acc, sat):
    acc = np.asarray(acc, dtype=np.int64)
    rr = rr_of(acc).astype(np.uint64)
    return (acc.astype(np.uint64) & np.uint64(0xFFFFF)) | (rr << np.uint64(20)) | \
        (np.asarray(sat).astype(np.uint64) << np.uint64(36))


def decode_word(w: int) -> str:
    acc = w & 0xFFFFF
    acc -= (1 << 20) if acc >> 19 else 0
    return f"ACC={acc} RR={signed16(w >> 20 & 0xFFFF)} SAT={w >> 36 & 7:03b}"


def spec_blocks(priors, keep: bool):
    """{(prior index, negative, c): checksum} and the per-case word arrays on demand."""
    n = 1 << 19 if keep else 1 << 16
    lo = -(1 << 18) if keep else -32768
    domain = np.arange(lo, lo + n, dtype=np.int64)
    x = divide(domain, 6) if keep else domain * 8
    if keep and MUTATION == "keep_shift_by_16":
        x = domain >> 4
    acc0 = np.array([0 if p == "Z" else p for p in priors], dtype=np.int64)[:, None]
    w = weights(n)
    sums = {}
    STATS[f"{'keep' if keep else 'exhaustive'} cases with SAT"] = 0
    for negative in (0, 1):
        for c in range(64):
            acc, sat = row_product(acc0, x[None, :], c, bool(negative))
            acc = np.broadcast_to(acc, (len(priors), n))
            sat = np.broadcast_to(sat, (len(priors), n))
            STATS[f"{'keep' if keep else 'exhaustive'} cases with SAT"] += int(np.count_nonzero(sat))
            block = (words(acc, sat) * w[None, :]).sum(axis=1, dtype=np.uint64)
            for p in range(len(priors)):
                sums[(p, negative, c)] = int(block[p])
    return sums, domain


def spec_block_words(prior, keep: bool, negative: int, c: int):
    n = 1 << 19 if keep else 1 << 16
    lo = -(1 << 18) if keep else -32768
    domain = np.arange(lo, lo + n, dtype=np.int64)
    x = divide(domain, 6) if keep else domain * 8
    if keep and MUTATION == "keep_shift_by_16":
        x = domain >> 4
    acc0 = np.int64(0 if prior == "Z" else prior)
    acc, sat = row_product(np.full(n, acc0), x, c, bool(negative))
    return domain, x, words(acc, np.broadcast_to(sat, (n,)))


def self_check(count: int = 200_000, seed: int = 7):
    """The vectorized and scalar forms of the spec agree (both are the spec)."""
    rng = np.random.default_rng(seed)
    acc = rng.integers(ACC_MIN, ACC_MAX + 1, count)
    x = rng.integers(-32768, 32768, count) * 8
    cs = rng.integers(0, 64, count)
    ns = rng.integers(0, 2, count)
    bad = 0
    for i in range(count):
        a, s = row_product(int(acc[i]), int(x[i]), int(cs[i]), bool(ns[i]))
        av, sv = row_product(np.array([acc[i]]), np.array([x[i]]), int(cs[i]), bool(ns[i]))
        bad += a != int(av[0]) or s != int(np.broadcast_to(sv, (1,))[0])
    return bad


# ---------------------------------------------------------------------------
# Building and running the implementations.
# ---------------------------------------------------------------------------
def build_cpp():
    binary = BUILD / "arith_exhaustive_cpp"
    subprocess.run(["clang++", "-std=c++20", "-O2", "-Wall", "-Wextra", "-o", str(binary),
                    str(ROOT / "tests/arith_exhaustive.cpp")], check=True)
    return binary


def build_sv(top: str, sources):
    compiler, env = run.verilator()
    directory = BUILD / top
    done = subprocess.run([str(compiler), "--binary", "--timing", "-O3", "-Wall",
                    "-Wno-DECLFILENAME", "-Wno-UNUSEDSIGNAL", "-Wno-PROCASSINIT",
                    "-Wno-UNUSEDPARAM",  # the functions harness leaves the converter constants unused
                    "--top-module", top, "--Mdir", str(directory),
                    "-CFLAGS", "-std=c++20 -O2", "-j", "4", *map(str, sources)],
                   env=env, capture_output=True, text=True)
    if done.returncode:
        sys.exit(f"Verilator build of {top} failed:\n{done.stdout}{done.stderr}")
    return directory / f"V{top}"


def prior_args(priors):
    return [str(p) for p in priors]


def parse_blocks(text: str):
    sums = {}
    for line in text.splitlines():
        parts = line.split()
        if len(parts) == 4 and parts[0].isdigit():
            sums[(int(parts[0]), int(parts[1]), int(parts[2]))] = int(parts[3], 16)
    return sums


def hex_words(text: str):
    return [int(t, 16) for t in text.split() if len(t) == 16]


def timed(command):
    started = time.monotonic()
    out = subprocess.run(command, check=True, capture_output=True, text=True).stdout
    return out, time.monotonic() - started


def compare_blocks(name, kind, spec, theirs, priors, dump, limit=8):
    """Returns the number of mismatching blocks; prints the first cases of the first one."""
    bad = sorted(k for k in spec if theirs.get(k) != spec[k])
    missing = len(spec) - sum(1 for k in spec if k in theirs)
    if missing:
        print(f"  {name}: {missing} blocks missing from the output")
    if bad and dump is not None:
        p, n, c = bad[0]
        domain, x, expected = spec_block_words(priors[p], kind == "keep", n, c)
        got = dump(kind, priors[p], n, c)
        shown = 0
        for i, (e, g) in enumerate(zip(expected.tolist(), got)):
            if e != g:
                label = f"x_prev={int(domain[i])} x={int(x[i])}" if kind == "keep" else f"R[RA]={int(domain[i])}"
                print(f"    prior={priors[p]} negative={n} c={c} {label}: spec {decode_word(e)} | "
                      f"{name} {decode_word(g)}")
                shown += 1
                if shown >= limit:
                    break
        if len(got) != len(expected):
            print(f"    {name} dumped {len(got)} words, expected {len(expected)}")
    return len(bad) + missing


# ---------------------------------------------------------------------------
# Chains.
# ---------------------------------------------------------------------------
EDGE_SAMPLES = [0x8000, 0x7FFF, 0xFFFF, 0x0000, 0x0001, 0xC000, 0x4000, 0x8001, 0x3FFF, 0xFFF8]
EDGE_COEFFICIENTS = [0, 1, 2, 3, 31, 32, 33, 48, 62, 63, 21, 42]


def instruction(c, zero, xfer, negative, ra, wa, op, low):
    return c << 26 | zero << 25 | xfer << 24 | negative << 23 | ra << 20 | wa << 18 | op << 16 | low


def generate_chains(chains: int, seed: int):
    """Rows (word, xreg input), grouped into chains of 2-6 rows: the first row
    of a chain is a ZERO row (usually with XFER, saving the previous chain);
    later rows XFER at random. Padded to whole 128-row programs."""
    rng = random.Random(seed)
    offsets = [rng.getrandbits(16) for _ in range(8)]
    rows, samples = [], []
    kinds = ["load"] * 6 + ["rr"] * 2 + ["nop", "source0", "memw", "memr"]
    for _ in range(chains):
        for i in range(rng.randint(2, 6)):
            kind = rng.choice(kinds)
            c = rng.choice(EDGE_COEFFICIENTS) if rng.random() < 0.3 else rng.getrandbits(6)
            zero = int(i == 0)
            xfer = int(rng.random() < (0.75 if i == 0 else 0.15))
            negative = rng.getrandbits(1)
            wa = rng.getrandbits(2)
            ra = wa if (kind == "load" and rng.random() < 0.6) else rng.getrandbits(2)
            keep = int(rng.random() < 0.2)
            if kind == "load":
                word = instruction(c, zero, xfer, negative, ra, wa, 1,
                                   2 << 12 | int(rng.random() < 0.1) << 6 | keep << 4)
            elif kind == "rr":
                word = instruction(c, zero, xfer, negative, ra, wa, 1, 1 << 12 | 1 << 6 | keep << 4)
            elif kind == "source0":
                word = instruction(c, zero, xfer, negative, ra, wa, 1, int(rng.random() < 0.5) << 6 | keep << 4)
            elif kind == "nop":
                word = instruction(c, zero, xfer, negative, ra, wa, 0, rng.getrandbits(16))
            else:
                word = instruction(c, zero, xfer, negative, ra, wa, 2 if kind == "memw" else 3, rng.choice(offsets))
            sample = rng.choice(EDGE_SAMPLES) if rng.random() < 0.3 else rng.getrandbits(16)
            rows.append(word)
            samples.append(sample)
    while len(rows) % 128:
        rows.append(0)
        samples.append(0)
    return Rows(np.array(rows, dtype=np.uint32), np.array(samples, dtype=np.uint16))


class Rows:
    """The chain stream: one instruction word and one XREG input per row."""

    def __init__(self, words, samples):
        self.words, self.samples = words, samples

    def __len__(self):
        return len(self.words)

    def __getitem__(self, i):
        return int(self.words[i]), int(self.samples[i])


def write_chain_file(rows, path: Path):
    """uint32 programs; per program the 512-byte CPU-order image (row a at
    (a ^ 127) * 4, bytes complemented, as isa-level-cpp/static_run.cpp loads it) and 128
    little-endian XREG inputs."""
    programs = len(rows) // 128
    words = rows.words.reshape(programs, 128)
    image = (~words)[:, ::-1].astype("<u4").view(np.uint8).reshape(programs, 512)
    inputs = rows.samples.reshape(programs, 128).astype("<u2").view(np.uint8).reshape(programs, 256)
    with open(path, "wb") as out:
        out.write(struct.pack("<I", programs))
        out.write(np.concatenate([image, inputs], axis=1).tobytes())


def spec_chains(rows, programs=None):
    """Per program: (C++ view checksum, SV view checksum), and per-row records
    (C++ view w1, SV view w1, w2) for the diagnostics."""
    m = RowMachine()
    previous_sat = 0
    cpp_sums, sv_sums = [], []
    total = len(rows) // 128 if programs is None else programs
    records = np.zeros((total * 128, 3), dtype=np.uint64)
    wts = [weight(i) for i in range(256)]
    for p in range(total):
        cpp_sum = sv_sum = 0
        words = rows.words[p * 128:(p + 1) * 128].tolist()
        samples = rows.samples[p * 128:(p + 1) * 128].tolist()
        block = []
        for r in range(128):
            word = words[r]
            acc_before, sat = m.row(word, samples[r])
            op = word >> 16 & 3
            for name, hit in (("ZERO rows", word >> 25 & 1), ("XFER rows", word >> 24 & 1),
                              ("negative rows", word >> 23 & 1),
                              ("keep-shifting rows", op == 1 and word >> 4 & 1),
                              ("RR-source rows", op == 1 and (word >> 12 & 3) == 1),
                              ("MEMW rows", op == 2), ("MEMR rows", op == 3),
                              ("rows with a SAT add", sat != 0), ("rows ending at a rail", m.ACC in (ACC_MIN, ACC_MAX))):
                if hit:
                    STATS[name] = STATS.get(name, 0) + 1
            base = m.RR | m.xreg_out << 40
            w2 = m.R[0] | m.R[1] << 16 | m.R[2] << 32 | m.R[3] << 48
            cpp_w1 = base | (acc_before & 0xFFFFF) << 16 | previous_sat << 36
            sv_w1 = base | (m.ACC & 0xFFFFF) << 16
            cpp_sum += cpp_w1 * wts[2 * r] + w2 * wts[2 * r + 1]
            sv_sum += sv_w1 * wts[2 * r] + w2 * wts[2 * r + 1]
            block.append((cpp_w1, sv_w1, w2))
            previous_sat = sat
        records[p * 128:(p + 1) * 128] = np.array(block, dtype=np.uint64)
        cpp_sums.append(cpp_sum & MASK64)
        sv_sums.append(sv_sum & MASK64)
    return cpp_sums, sv_sums, records


def describe_row(word: int, sample: int) -> str:
    op = ["NOP", "OPER", "MEMW", "MEMR"][word >> 16 & 3]
    low = word & 0xFFFF
    text = (f"{op} c={word >> 26 & 63} zero={word >> 25 & 1} xfer={word >> 24 & 1} neg={word >> 23 & 1} "
            f"ra={word >> 20 & 3} wa={word >> 18 & 3}")
    if op == "OPER":
        text += f" source={low >> 12 & 3} wr_xreg={low >> 6 & 1} keep={low >> 4 & 1} xreg_in={signed16(sample)}"
    elif op in ("MEMW", "MEMR"):
        text += f" field={low:04x}"
    return text


def describe_state(w1: int, w2: int, view: str) -> str:
    acc = w1 >> 16 & 0xFFFFF
    acc -= (1 << 20) if acc >> 19 else 0
    sat = f" SAT(prev row)={w1 >> 36 & 7:03b}" if view == "cpp" else ""
    regs = [signed16(w2 >> (16 * k) & 0xFFFF) for k in range(4)]
    return f"RR={signed16(w1 & 0xFFFF)} ACC={acc}{sat} XREGout={signed16(w1 >> 40 & 0xFFFF)} R={regs}"


def compare_chains(name, view, spec_sums, theirs, rows, records, dump, limit=6):
    bad = [p for p in range(len(spec_sums)) if theirs.get(p) != spec_sums[p]]
    if bad and dump is not None:
        p = bad[0]
        got = dump(p)
        shown = 0
        for r in range(128):
            spec_w1 = int(records[p * 128 + r][0 if view == "cpp" else 1])
            spec_w2 = int(records[p * 128 + r][2])
            g1, g2 = got.get(r, (None, None))
            if (g1, g2) != (spec_w1, spec_w2):
                print(f"    program {p} row {r}: {describe_row(*rows[p * 128 + r])}")
                if r:
                    print(f"      previous row: {describe_row(*rows[p * 128 + r - 1])}")
                print(f"      spec: {describe_state(spec_w1, spec_w2, view)}")
                if g1 is not None:
                    print(f"      {name}: {describe_state(g1, g2, view)}")
                shown += 1
                if shown >= limit:
                    break
    return bad


def parse_chain_sums(text: str):
    out = {}
    for line in text.splitlines():
        parts = line.split()
        if len(parts) == 2 and parts[0].isdigit():
            out[int(parts[0])] = int(parts[1], 16)
    return out


def parse_chain_dump(text: str):
    out = {}
    for line in text.splitlines():
        parts = line.split()
        if len(parts) == 3 and parts[0].isdigit():
            out[int(parts[0])] = (int(parts[1], 16), int(parts[2], 16))
    return out


# ---------------------------------------------------------------------------
def implementations(args):
    """Build and run every implementation once; results cached for --mutate."""
    BUILD.mkdir(parents=True, exist_ok=True)
    started = time.monotonic()
    with ThreadPoolExecutor(3) as pool:
        cpp_f = pool.submit(build_cpp)
        sv_f = pool.submit(build_sv, "arith_exhaustive",
                           [ROOT / "lexicon224x.sv", ROOT / "tests/arith_exhaustive.sv"])
        chain_f = pool.submit(build_sv, "arith_chain",
                              [ROOT / "lexicon224x.sv", ROOT / "dsp.sv", ROOT / "tests/arith_chain.sv"])
        cpp, sv, sv_chain = cpp_f.result(), sv_f.result(), chain_f.result()
    print(f"built C++ and 2 Verilator models in {time.monotonic() - started:.1f} s")

    (BUILD / "priors.txt").write_text("\n".join(prior_args(PRIORS)) + "\n")
    (BUILD / "keep_priors.txt").write_text("\n".join(prior_args(KEEP_PRIORS)) + "\n")
    rows = generate_chains(args.chains, args.seed)
    chain_file = BUILD / "chains.bin"
    write_chain_file(rows, chain_file)

    jobs = {
        ("exhaustive", "C++ helpers"): [str(cpp), "exhaustive", "helpers", *prior_args(PRIORS)],
        ("exhaustive", "C++ aruck"): [str(cpp), "exhaustive", "aruck", *prior_args(PRIORS)],
        ("exhaustive", "C++ machine"): [str(cpp), "exhaustive", "machine", *prior_args(PRIORS)],
        ("exhaustive", "SV functions"): [str(sv), "+mode=exhaustive", f"+priors={BUILD / 'priors.txt'}"],
        ("keep", "C++ helpers"): [str(cpp), "keep", "helpers", *prior_args(KEEP_PRIORS)],
        ("keep", "C++ aruck"): [str(cpp), "keep", "aruck", *prior_args(KEEP_PRIORS)],
        ("keep", "SV functions"): [str(sv), "+mode=keep", f"+priors={BUILD / 'keep_priors.txt'}"],
        ("chain", "C++ Machine"): [str(cpp), "chain", str(chain_file)],
        ("chain", "SV dsp.sv"): [str(sv_chain), f"+chain={chain_file}"],
    }
    results = {}
    with ThreadPoolExecutor(min(len(jobs), os.cpu_count() or 4)) as pool:
        futures = {key: pool.submit(timed, command) for key, command in jobs.items()}
        for key, future in futures.items():
            results[key] = future.result()
    return {"cpp": cpp, "sv": sv, "sv_chain": sv_chain, "rows": rows, "chain_file": chain_file,
            "results": results}


def run_checks(impl, args, quiet=False):
    """Compare the spec (under MUTATION) with every implementation. Returns failures."""
    say = (lambda *a: None) if quiet else print
    cpp, sv, sv_chain = impl["cpp"], impl["sv"], impl["sv_chain"]
    failures = 0
    parts = {}  # failing blocks/programs per (part, implementation)

    def dump_cpp(route):
        def dump(kind, prior, n, c):
            out = subprocess.run([str(cpp), "dump", kind, route, str(prior), str(n), str(c)],
                                 check=True, capture_output=True, text=True).stdout
            return hex_words(out)
        return dump

    def dump_sv(kind, prior, n, c):
        index = (PRIORS if kind == "exhaustive" else KEEP_PRIORS).index(prior)
        out = subprocess.run([str(sv), f"+mode={kind}", "+dump=1", f"+dp={index}", f"+dn={n}", f"+dc={c}",
                              f"+priors={BUILD / ('priors.txt' if kind == 'exhaustive' else 'keep_priors.txt')}"],
                             check=True, capture_output=True, text=True).stdout
        return hex_words(out)

    for kind, priors in (("exhaustive", PRIORS), ("keep", KEEP_PRIORS)):
        started = time.monotonic()
        spec, _ = spec_blocks(priors, kind == "keep")
        spec_seconds = time.monotonic() - started
        cases = len(spec) * (1 << 19 if kind == "keep" else 1 << 16)
        say(f"{kind}: {len(priors)} priors x 2 signs x 64 coefficients = {len(spec)} blocks, "
            f"{cases:,} cases (spec {spec_seconds:.1f} s)")
        for (k, name), (out, seconds) in impl["results"].items():
            if k != kind:
                continue
            theirs = parse_blocks(out)
            dump = dump_sv if name.startswith("SV") else dump_cpp(name.split()[1])
            bad = compare_blocks(name, kind, spec, theirs, priors, None if quiet else dump)
            extra = ""
            if name.startswith("SV"):
                internal = [line for line in out.splitlines() if line.startswith("internal")]
                count = int(internal[0].split()[1]) if internal else -1
                extra = f"; multiply_accumulate vs its three steps: {count} differences"
                if count != 0:
                    failures += 1
            parts[f"{kind}/{name}"] = bad
            say(f"  {name:13s} {'agrees' if not bad else f'DISAGREES in {bad} blocks'} "
                f"({seconds:.1f} s){extra}")
            failures += bad

    rows = impl["rows"]
    started = time.monotonic()
    programs = len(rows) // 128 if args.mutant_programs is None else min(args.mutant_programs, len(rows) // 128)
    cpp_sums, sv_sums, records = spec_chains(rows, programs)
    say(f"chains: {args.chains:,} chains, {len(rows):,} rows in {len(rows) // 128:,} programs "
        f"(spec {time.monotonic() - started:.1f} s over {programs:,} programs)")
    for name, view, sums, command in (("C++ Machine", "cpp", cpp_sums, lambda p: [str(cpp), "chain",
                                                                                  str(impl["chain_file"]), str(p)]),
                                      ("SV dsp.sv", "sv", sv_sums, lambda p: [str(sv_chain),
                                                                              f"+chain={impl['chain_file']}",
                                                                              f"+dump={p}"])):
        out, seconds = impl["results"][("chain", name)]
        theirs = parse_chain_sums(out)
        dump = None if quiet else (lambda p, command=command: parse_chain_dump(
            subprocess.run(command(p), check=True, capture_output=True, text=True).stdout))
        bad = compare_chains(name, view, sums, theirs, rows, records, dump)
        say(f"  {name:13s} {'agrees' if not bad else f'DISAGREES in {len(bad)} programs'} "
            f"on {len(sums):,} programs x 128 rows ({seconds:.1f} s)")
        failures += len(bad)
        parts[f"chain/{name}"] = len(bad)
    run_checks.parts = parts
    return failures


def main():
    global MUTATION
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--chains", type=int, default=1_000_000)
    parser.add_argument("--seed", type=int, default=1)
    parser.add_argument("--mutate", choices=[*MUTANTS, "all"], help="break the spec on purpose: the check must fail")
    parser.add_argument("--mutant-programs", type=int, default=None,
                        help="chain programs checked per mutant (default: all; 'all' mode uses 2000)")
    parser.add_argument("--ambiguities", action="store_true", help="print how the spec reads the prose, and exit")
    args = parser.parse_args()
    if args.ambiguities:
        print(AMBIGUITIES)
        return 0
    started = time.monotonic()

    bad = self_check()
    print(f"spec self-check (scalar vs vector form, 200,000 random cases): {bad} differences")
    if bad:
        return 1
    impl = implementations(args)

    if args.mutate == "all":
        if args.mutant_programs is None:
            args.mutant_programs = 2000
        print(f"negative controls (chain part limited to {args.mutant_programs} programs):")
        caught = 0
        for name, what in MUTANTS.items():
            MUTATION = name
            t = time.monotonic()
            failures = run_checks(impl, args, quiet=True)
            MUTATION = None
            caught += failures > 0
            where = ", ".join(f"{k} {v}" for k, v in run_checks.parts.items() if v)
            print(f"  {'CAUGHT' if failures else 'MISSED'} {name}: {what} ({time.monotonic() - t:.1f} s)\n"
                  f"         failing blocks/programs: {where or 'none'}")
        print(f"{caught} of {len(MUTANTS)} negative controls caught "
              f"({time.monotonic() - started:.1f} s total)")
        return 0 if caught == len(MUTANTS) else 1

    MUTATION = args.mutate
    if MUTATION:
        print(f"MUTATED SPEC: {MUTATION}: {MUTANTS[MUTATION]} (this run must fail)")
    failures = run_checks(impl, args)
    print("coverage: " + ", ".join(f"{k} {v:,}" for k, v in STATS.items()))
    print(f"{'ALL AGREE' if not failures else f'{failures} FAILING BLOCKS/PROGRAMS'} "
          f"({time.monotonic() - started:.1f} s total)")
    return 0 if not failures else 1


if __name__ == "__main__":
    sys.exit(main())
