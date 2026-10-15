#!/usr/bin/env python3
"""Run one CI check with a deadline and preserve its output even on timeout.

Use a regular log file: an inherited pipe in a diagnostic subprocess must not
keep tee/communicate waiting after the check itself has exited.
"""
import re
import shutil
import subprocess
import sys
from pathlib import Path


def main():
    if len(sys.argv) < 4:
        raise SystemExit("Usage: run-bounded.py SECONDS LOG COMMAND [ARG...]")
    seconds = float(sys.argv[1])
    if seconds <= 0:
        raise SystemExit("Deadline must be positive")
    path = Path(sys.argv[2])
    path.parent.mkdir(parents=True, exist_ok=True)
    with path.open("w") as output:
        try:
            result = subprocess.run(sys.argv[3:], timeout=seconds,
                                    stdout=output, stderr=subprocess.STDOUT)
            status = result.returncode
        except subprocess.TimeoutExpired:
            output.write(f"\nFAIL: deadline exceeded ({seconds:g} seconds); child terminated\n")
            status = 124
    captured = path.read_text(errors="replace")
    print(captured, end="", flush=True)
    # Fatal test diagnostics retain module-relative PCs even for PIE binaries.
    # Resolve a bounded backtrace using standard binutils when CI provides it.
    resolver = shutil.which("addr2line")
    offsets = re.findall(r"\(\+(0x[0-9a-fA-F]+)\)", captured)
    if status and resolver and offsets:
        try:
            subprocess.run([resolver, "-f", "-C", "-e", sys.argv[3], *offsets[:24]],
                           timeout=10, check=False)
        except subprocess.TimeoutExpired:
            print("Native backtrace symbolization exceeded 10 seconds", flush=True)
    return status


if __name__ == "__main__":
    raise SystemExit(main())

