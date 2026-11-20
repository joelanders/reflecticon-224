#!/usr/bin/env python3
"""Sample a child simulation on macOS; the child runs to completion.

Use a separate, unprofiled invocation for performance measurements.
"""
import argparse
from pathlib import Path
import shutil
import subprocess
import time


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path, default=Path("build/profile.txt"))
    parser.add_argument("--log", type=Path, default=Path("build/profile-workload.log"))
    parser.add_argument("--after", help="Start sampling when this text appears in the child's stdout log")
    parser.add_argument("--seconds", type=int, default=5)
    parser.add_argument("command", nargs=argparse.REMAINDER)
    args = parser.parse_args()
    command = args.command[1:] if args.command[:1] == ["--"] else args.command
    if not command or args.seconds <= 0:
        parser.error("Supply a command after -- and a positive sampling duration")
    sampler = shutil.which("sample")
    if not sampler:
        parser.error("This wrapper uses the macOS sample tool")
    with args.log.open("w") as log:
        child = subprocess.Popen(command, stdout=log, stderr=subprocess.STDOUT)
        print(f"Simulation PID {child.pid}; log {args.log}", flush=True)
        if args.after:
            while child.poll() is None and args.after not in args.log.read_text():
                time.sleep(0.1)
            if args.after not in args.log.read_text():
                raise RuntimeError("Simulation ended before the requested profile point")
        sampled = subprocess.run([sampler, str(child.pid), str(args.seconds), "1", "-file", str(args.output)])
        status = child.wait()
        sampled.check_returncode()
        if status:
            raise RuntimeError(f"Simulation exited with status {status}; see {args.log}")


if __name__ == "__main__":
    main()
