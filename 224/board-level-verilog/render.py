#!/usr/bin/env python3
"""Render the four digital AOUT holds to PCM16 WAV by holding each sample.

This is an inspection artifact: no analog reconstruction filter or high
quality sample-rate converter is implied. Unknown holds become silence and
are counted in the accompanying JSON report.
"""
from __future__ import annotations

import argparse
from array import array
import csv
import json
from pathlib import Path
import sys
import wave

TICKS_PER_SECOND = 576_000_000_000


def render(prefix: Path, destination: Path, *, rate=48000, start=0.0, duration=None):
    summary = json.loads(Path(str(prefix) + ".summary.json").read_text())
    start_tick = round(start * TICKS_PER_SECOND)
    end_tick = summary["end_tick"]
    if duration is not None:
        end_tick = min(end_tick, start_tick + round(duration * TICKS_PER_SECOND))
    if rate <= 0 or start_tick < 0 or end_tick <= start_tick:
        raise ValueError("Rendering needs a positive rate and an interval inside the trace")
    frames = (end_tick - start_tick) * rate // TICKS_PER_SECOND
    counts = [dict(unknown=0, nonzero=0, peak=0, signed_sum=0, square_sum=0, clipped=0) for _ in range(4)]
    with Path(str(prefix) + ".audio.csv").open(newline="") as source, wave.open(str(destination), "wb") as output:
        records = iter(csv.DictReader(source))
        event = next(records, None)
        samples, known = [0] * 4, [0] * 4
        output.setparams((4, 2, rate, frames, "NONE", "not compressed"))
        buffer = array("h")
        for frame in range(frames):
            tick = start_tick + frame * TICKS_PER_SECOND // rate
            while event is not None and int(event["tick"]) <= tick:
                samples = [int(event[f"sample{channel}"]) for channel in range(4)]
                known = [int(event[f"known{channel}"]) for channel in range(4)]
                event = next(records, None)
            for channel, metrics in enumerate(counts):
                if known[channel] != 65535:
                    metrics["unknown"] += 1
                    sample = 0
                else:
                    sample = samples[channel]
                    metrics["nonzero"] += sample != 0
                    metrics["peak"] = max(metrics["peak"], abs(sample))
                    metrics["signed_sum"] += sample
                    metrics["square_sum"] += sample * sample
                    metrics["clipped"] += sample in (-32768, 32767)
                buffer.append(sample)
            if len(buffer) >= 4096 or frame + 1 == frames:
                if sys.byteorder != "little":
                    buffer.byteswap()
                output.writeframesraw(buffer.tobytes())
                buffer = array("h")
    for metrics in counts:
        available = frames - metrics["unknown"]
        metrics["mean"] = metrics.pop("signed_sum") / available if available else None
        metrics["rms"] = (metrics.pop("square_sum") / available) ** 0.5 if available else None
    report = dict(source=str(prefix), start_tick=start_tick, end_tick=end_tick,
                  sample_rate=rate, frames=frames, channel_order="A B C D",
                  reconstruction="zero-order holds; unknown values written as silence", channels=counts)
    destination.with_suffix(".render.json").write_text(json.dumps(report, indent=2) + "\n")
    print(f"{destination}: {frames} frames, four channels; unknown frames per channel: {[x['unknown'] for x in counts]}")
    return report


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("prefix", type=Path, help="Prefix of .audio.csv and .summary.json")
    parser.add_argument("destination", type=Path)
    parser.add_argument("--rate", type=int, default=48000)
    parser.add_argument("--start", type=float, default=0)
    parser.add_argument("--duration", type=float)
    args = parser.parse_args()
    render(args.prefix, args.destination, rate=args.rate, start=args.start, duration=args.duration)


if __name__ == "__main__":
    main()
