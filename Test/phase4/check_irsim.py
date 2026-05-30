#!/usr/bin/env python3
"""irsim-based checker: verify .ir output against JSON test cases."""
import json
import sys
import subprocess
from itertools import count


def err(data_in, s):
    print(f'\033[1m\033[91mWrong on input {data_in}\n{s}\033[0m', file=sys.stderr)
    sys.exit(1)


def check(ir_file, json_file, irsim_bin):
    total_instructions = 0

    with open(json_file) as f:
        test_cases = json.load(f)

    for data_in, data_out, _ret_val in test_cases:
        input_str = '\n'.join(map(str, data_in)) + '\n'

        proc = subprocess.run(
            [irsim_bin, ir_file],
            input=input_str,
            text=True,
            capture_output=True
        )

        if proc.returncode != 0:
            err(data_in,
                f"runtime error running irsim\n{proc.stderr.strip() or proc.stdout.strip()}")

        out_lines = [line.strip() for line in proc.stdout.strip().splitlines()]

        try:
            for idx, expect in zip(count(1), data_out):
                if idx - 1 >= len(out_lines):
                    err(data_in, "Output mismatch! (output less than expected)")
                user_out = int(out_lines[idx - 1])
                if expect != user_out:
                    err(data_in,
                        f"Output mismatch! expected {expect}, found {user_out} at line {idx}")

            if len(out_lines) <= len(data_out):
                err(data_in, "Output mismatch! (output less than expected)")

            ret_line = out_lines[len(data_out)]
            if not ret_line.startswith("return"):
                err(data_in, "Output mismatch! (output more than expected)")

            single = int(ret_line.split()[3])
            total_instructions += single

        except ValueError:
            err(data_in, "Output mismatch! (formatting error)")

    print(total_instructions)
    sys.exit(0)


if __name__ == "__main__":
    if len(sys.argv) < 3:
        print("Usage: python3 check_irsim.py <ir_file> <json_file> [irsim_bin]",
              file=sys.stderr)
        sys.exit(1)

    ir_file = sys.argv[1]
    json_file = sys.argv[2]
    irsim_bin = sys.argv[3] if len(sys.argv) > 3 else "irsim/build/irsim"
    check(ir_file, json_file, irsim_bin)