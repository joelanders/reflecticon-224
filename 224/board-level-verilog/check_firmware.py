#!/usr/bin/env python3
"""Check observed v8.1 diagnostic returns and the scripted MAX DELAY run.

The addresses below are observers of caller-supplied firmware. They never
change instruction execution or seed a result into the hardware model.
"""
import argparse
import csv
import hashlib
import json
from pathlib import Path

TICKS_PER_SECOND = 576_000_000_000
CPU_HZ = 2_048_000


def check_boot(log: Path):
    watches = {}
    for line in log.read_text().splitlines():
        fields = line.split()
        if fields and fields[0] == "P":
            watches[int(fields[2], 16)] = dict(cycle=int(fields[1]), a=int(fields[3], 16), flags=int(fields[4], 16))
    for address in (0x0294, 0x05CD, 0x05DD):
        if address in watches:
            raise RuntimeError(f"Firmware reached error observer {address:04x}: {watches[address]}")
    for address in (0x059C, 0x05C8, 0x05CC, 0x028C, 0x0B61):
        if address not in watches:
            raise RuntimeError(f"Missing firmware observer {address:04x}; run through the completed test")
    for address in (0x05CC, 0x028C):
        state = watches[address]
        if state["a"] or not state["flags"] & 0x40:
            raise RuntimeError(f"Diagnostic return {address:04x} does not indicate success: {state}")
    print("v8.1: both delay-memory readbacks passed; success return at CPU cycle", watches[0x05CC]["cycle"])
    return {f"{address:04x}": state for address, state in watches.items()}


def check_max_delay(prefix: Path):
    image = Path(str(prefix) + ".wcs.bin").read_bytes()
    digest = hashlib.sha256(image).hexdigest()
    if digest != "8427275671975eb34a1f11bd1628afaaefc54147f3e2e9d8d0b123606c22d816":
        raise RuntimeError("Firmware did not select the recorded v8.1 MAX DELAY program")
    summary = json.loads(Path(str(prefix) + ".summary.json").read_text())
    if summary["source_overlap_events"] or summary["undriven_source_events"]:
        raise RuntimeError("An audio conversion accepted contended or undriven DAB")
    pulses = {}
    for line in Path(str(prefix) + ".log").read_text().splitlines():
        fields = line.split()
        if len(fields) == 7 and fields[0] == "E" and fields[2] == "audio":
            if int(fields[3]):
                pulses["left"] = int(fields[1]) / CPU_HZ
            if int(fields[5]):
                pulses["right"] = int(fields[1]) / CPU_HZ
    if set(pulses) != {"left", "right"}:
        raise RuntimeError("Both scripted input pulses are required")
    channels = [dict(first=None, last=None, values=set()) for _ in range(4)]
    with Path(str(prefix) + ".audio.csv").open() as stream:
        for event in csv.DictReader(stream):
            for index, channel in enumerate(channels):
                if not int(event["channels"]) & (1 << index):
                    continue
                if int(event[f"known{index}"]) != 65535:
                    raise RuntimeError(f"Unknown output on channel {index} at {event['tick']}")
                sample = int(event[f"sample{index}"])
                if sample:
                    seconds = int(event["tick"]) / TICKS_PER_SECOND
                    if channel["first"] is None:
                        channel["first"] = seconds
                    channel["last"] = seconds
                    channel["values"].add(sample)
    for index, channel in enumerate(channels):
        side, value = ("right", -6144) if index in (0, 3) else ("left", 8192)
        if channel["values"] != {value}:
            raise RuntimeError(f"Channel {index} amplitude/routing differs: {channel['values']}")
        delay = channel["first"] - pulses[side]
        width = channel["last"] - channel["first"]
        if abs(delay - 0.480) > 0.000050 or abs(width - 0.002) > 0.000060:
            raise RuntimeError(f"Channel {index}: delay={delay}, pulse width={width}")
        if channel["last"] > summary["end_tick"] / TICKS_PER_SECOND - 0.1:
            raise RuntimeError(f"Channel {index} has not returned to silence for 100 ms")
        channel.update(values=sorted(channel["values"]), input=side, delay_seconds=delay, pulse_width_seconds=width)
    print("v8.1 MAX DELAY: four known channels, expected amplitudes/routing, 480 ms delay, silence after both pulses")
    return dict(wcs_sha256=digest, channels=channels)


def check_setup(boot_log: Path, reference: Path, cycles: int):
    native = [line for line in boot_log.with_suffix(".events").read_text().splitlines()
              if int(line.split()[1]) <= cycles * 281250]
    expected = reference.read_text().splitlines()
    if not expected or len(native) != len(expected):
        raise RuntimeError(f"Boot setup event counts differ or are empty: {len(native)} / {len(expected)}")
    for index, (actual, wanted) in enumerate(zip(native, expected)):
        if actual != wanted:
            raise RuntimeError(f"Boot setup event {index}:\nSV  {actual}\nref {wanted}")
    print(f"v8.1 setup: {len(native)} exact reference audio events, including value, knownness, high-Z and timing")
    return dict(cpu_cycles=cycles, exact_events=len(native))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--boot-log", type=Path)
    parser.add_argument("--max-delay", type=Path, help="Prefix including .log, .wcs.bin, .audio.csv, .summary.json")
    parser.add_argument("--setup-reference", type=Path, help="a reference trace of the boot setup interval")
    parser.add_argument("--setup-cycles", type=int, default=6200000)
    parser.add_argument("--report", type=Path)
    args = parser.parse_args()
    if not args.boot_log and not args.max_delay:
        parser.error("Supply at least one observed run")
    if args.setup_reference and not args.boot_log:
        parser.error("--setup-reference requires --boot-log")
    report = {}
    if args.boot_log:
        report["boot"] = check_boot(args.boot_log)
    if args.max_delay:
        report["max_delay"] = check_max_delay(args.max_delay)
    if args.setup_reference:
        report["setup"] = check_setup(args.boot_log, args.setup_reference, args.setup_cycles)
    if args.report:
        args.report.write_text(json.dumps(report, indent=2) + "\n")


if __name__ == "__main__":
    main()
