#!/usr/bin/env python3
"""SPIM-based checker: verify .s (MIPS assembly) output against JSON test cases."""
import json
import sys
import subprocess
from itertools import count, zip_longest


def err(data_in, s):
    print(f'\033[1m\033[91mWrong on input {data_in}\n{s}\033[0m', file=sys.stderr)
    sys.exit(1)


def extract_ints(lines):
    """Extract integer values from spim output lines.

    SPIM outputs a banner (SPIM Version..., Copyright..., Loaded: ...),
    and prompts like "Enter an integer:" when reading. The actual program
    output from write() calls should be integers (one per line, assuming
    the MIPS codegen prints a newline after each write).
    """
    for line in lines:
        line = line.strip()
        if not line:
            continue
        if line == "Enter an integer:":
            continue
        # Skip known spim banner/error patterns
        if line.startswith(("SPIM Version", "Copyright", "All Rights",
                           "See the file", "Loaded:", "Can't", "Attempt",
                           "Exception", "PC =", "Error")):
            continue
        try:
            yield int(line)
        except ValueError:
            pass


def check(asm_file, json_file, timeout=30):
    with open(json_file) as f:
        test_cases = json.load(f)

    for data_in, data_out, _ret_val in test_cases:
        input_str = '\n'.join(map(str, data_in)) + '\n'

        try:
            proc = subprocess.run(
                ["spim", "-lstack", "1048576", "-stext", "1048576",
                 "-file", asm_file],
                input=input_str,
                text=True,
                capture_output=True,
                timeout=timeout
            )
        except subprocess.TimeoutExpired:
            err(data_in, "TLE (spim execution timed out)")

        if proc.returncode != 0:
            err(data_in,
                f"RE (spim returned {proc.returncode})\n"
                f"{proc.stderr.strip() or proc.stdout.strip()}")

        out_lines = proc.stdout.strip().splitlines()
        out_ints = list(extract_ints(out_lines))

        for idx, (expect, user_out) in enumerate(
                zip_longest(data_out, out_ints)):
            if expect is None:
                err(data_in,
                    f"Output mismatch! expected {len(data_out)} values, "
                    f"got more ({user_out} at position {idx})")
            if user_out is None:
                err(data_in,
                    f"Output mismatch! expected {expect} at line {idx + 1}, "
                    f"but got no more output")
            if expect != user_out:
                err(data_in,
                    f"Output mismatch! expected {expect}, "
                    f"found {user_out} at line {idx + 1}")

    # SPIM doesn't easily report user instruction count, output 0
    print(0)
    sys.exit(0)


if __name__ == "__main__":
    if len(sys.argv) < 3:
        print("Usage: python3 check_spim.py <asm_file> <json_file> [timeout]",
              file=sys.stderr)
        sys.exit(1)

    asm_file = sys.argv[1]
    json_file = sys.argv[2]
    timeout = int(sys.argv[3]) if len(sys.argv) > 3 else 30
    check(asm_file, json_file, timeout)