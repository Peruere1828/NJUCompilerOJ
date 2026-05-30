#!/usr/bin/env python3
"""
Phase 4 Parallel Test Runner for C-- Compiler.

Verifies both IR output (via irsim) and MIPS assembly (via spim) against
expected test case data from test_framework/lab3/tests/.

Usage:
    python3 test.py -r ../../parser -e base       # base tests only
    python3 test.py -r ../../parser -e both -a    # all extend + advance
    python3 test.py -r ../../parser -s            # quick smoke tests only
"""
import argparse
import subprocess
import sys
import os
import threading
from pathlib import Path
from concurrent.futures import ThreadPoolExecutor, as_completed

RED, GREEN, YELLOW, NC = '\033[0;31m', '\033[0;32m', '\033[0;33m', '\033[0m'
BOLD, NORMAL = '\033[1m', '\033[0m'

# --- Paths (relative to this script's location) ---
SCRIPT_DIR = Path(__file__).resolve().parent
PROJECT_ROOT = SCRIPT_DIR.parent.parent
LAB3_TESTS = PROJECT_ROOT / "test_framework" / "lab3" / "tests"
IRSIM_BIN = PROJECT_ROOT / "test_framework" / "lab3" / "irsim" / "build" / "irsim"
CHECK_IRSIM = SCRIPT_DIR / "check_irsim.py"
CHECK_SPIM = SCRIPT_DIR / "check_spim.py"
SMOKE_DIR = SCRIPT_DIR / "test_cases"


def parse_args():
    parser = argparse.ArgumentParser(
        description="Phase 4 Compiler Parallel Test Runner (irsim + spim)")
    parser.add_argument("-r", "--run", required=True,
                        help="Path to parser executable")
    parser.add_argument("-e", "--extend",
                        choices=['base', 'extend1', 'extend2', 'both'],
                        default='base',
                        help="Extend test category (default: base)")
    parser.add_argument("-a", "--advance", action='store_true',
                        help="Test advance cases")
    parser.add_argument("-p", "--prf", action='store_true',
                        help="Test performance (prf) cases")
    parser.add_argument("-s", "--smoke", action='store_true',
                        help="Run quick smoke tests (test_cases/) only")
    parser.add_argument("--irsim-only", action='store_true',
                        help="Only verify with irsim, skip spim")
    parser.add_argument("--spim-only", action='store_true',
                        help="Only verify with spim, skip irsim")
    parser.add_argument("-c", "--continue-on-error", action='store_true',
                        help="Do not stop when a test fails")
    parser.add_argument("-t", "--timeout", type=int, default=30,
                        help="Timeout per test in seconds (default: 30)")
    parser.add_argument("-j", "--jobs", type=int, default=os.cpu_count(),
                        help="Number of parallel jobs (default: cpu count)")
    return parser.parse_args()


def build_irsim():
    """Ensure irsim is built."""
    irsim_dir = PROJECT_ROOT / "test_framework" / "lab3" / "irsim"
    result = subprocess.run(["make", "-C", str(irsim_dir)],
                            capture_output=True, text=True)
    if result.returncode != 0:
        print(f"{RED}Failed to build irsim:{NC}")
        print(result.stderr)
        sys.exit(1)


def get_tests(category):
    """Find .cmm files in a test category under lab3/tests/.

    Returns (normal_tests, prf_tests) tuple.
    category: e.g. 'base', 'advance', 'extend/1', 'extend/2', 'extend/both'
    """
    pattern = f"{category}/**/*.cmm"
    all_tests = list(LAB3_TESTS.glob(pattern))
    normal = [t for t in all_tests if 'prf' not in t.parts[:-1]]
    prf = [t for t in all_tests if 'prf' in t.parts[:-1]]
    return normal, prf


def add_tests(target_list, category_pair):
    """Add normal tests (and optionally prf tests) from a category pair."""
    target_list.extend(category_pair[0])
    if args.prf:
        target_list.extend(category_pair[1])


# --- Result tracking ---
class Results:
    def __init__(self):
        self.lock = threading.Lock()
        self.passed = 0
        self.total = 0
        self.irsim_inst = 0
        self.details = []  # (rel_path, status, message)

    def add(self, rel_path, passed, msg, inst=0):
        with self.lock:
            self.total += 1
            if passed:
                self.passed += 1
                self.irsim_inst += inst
            self.details.append((rel_path, passed, msg, inst))

    def summary(self):
        print(f"\n{BOLD}PASSED {self.passed}/{self.total}{NORMAL}")
        if self.irsim_inst > 0:
            print(f"irsim executes about {self.irsim_inst} instructions")


def run_single_test(cmm_file, expect_pass, parser_path, results):
    """Compile and verify a single test case."""
    rel_path = cmm_file.relative_to(LAB3_TESTS)
    stem = str(rel_path).replace(os.sep, '_').replace('.cmm', '')
    workdir = SCRIPT_DIR / "workdir"
    ir_file = workdir / f"{stem}.ir"
    s_file = workdir / f"{stem}.s"
    json_file = cmm_file.with_suffix('.json')

    # Clean up stale files
    for f in (ir_file, s_file):
        if f.exists():
            f.unlink()

    # Step 1: Compile
    try:
        proc = subprocess.run(
            [parser_path, str(cmm_file), str(s_file), str(ir_file)],
            timeout=args.timeout,
            stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT
        )
    except subprocess.TimeoutExpired:
        results.add(rel_path, False, "TLE (compilation)")
        return

    if proc.returncode != 0:
        results.add(rel_path, False,
                    f"RE (compiler exit {proc.returncode}): "
                    f"{proc.stdout.decode(errors='replace')[:200]}")
        return

    if expect_pass:
        # Step 2: Verify outputs exist
        if not s_file.exists():
            results.add(rel_path, False, "Should generate .s but missing")
            return
        if not ir_file.exists() and not args.spim_only:
            results.add(rel_path, False, "Should generate .ir but missing")
            return

        irsim_ok = True
        spim_ok = True
        total_inst = 0
        messages = []

        # Step 3: irsim verification
        if not args.spim_only and ir_file.exists():
            try:
                chk = subprocess.run(
                    ["python3", str(CHECK_IRSIM), str(ir_file),
                     str(json_file), str(IRSIM_BIN)],
                    timeout=args.timeout,
                    capture_output=True,
                    text=True
                )
                if chk.returncode == 0:
                    total_inst = int(chk.stdout.strip())
                    messages.append(f"irsim OK ({total_inst} inst)")
                else:
                    irsim_ok = False
                    messages.append(f"irsim FAIL: {chk.stderr.strip()[-150:]}")
            except subprocess.TimeoutExpired:
                irsim_ok = False
                messages.append("irsim TLE")

        # Step 4: spim verification
        if not args.irsim_only and s_file.exists():
            try:
                chk = subprocess.run(
                    ["python3", str(CHECK_SPIM), str(s_file),
                     str(json_file), str(args.timeout)],
                    timeout=args.timeout + 5,  # extra buffer for spim
                    capture_output=True,
                    text=True
                )
                if chk.returncode == 0:
                    messages.append("spim OK")
                else:
                    spim_ok = False
                    messages.append(f"spim FAIL: {chk.stderr.strip()[-150:]}")
            except subprocess.TimeoutExpired:
                spim_ok = False
                messages.append("spim TLE")

        passed = irsim_ok and spim_ok
        results.add(rel_path, passed, "; ".join(messages), total_inst)
    else:
        # expect_fail: compiler should reject this input
        if not s_file.exists():
            results.add(rel_path, True, "passed (fault test)")
        else:
            results.add(rel_path, False, "Should not translate")


# --- Smoke tests: simple compilation + spim check ---
def run_smoke_tests(parser_path, timeout):
    """Quick compilation-and-run smoke test without JSON data."""
    if not SMOKE_DIR.exists():
        print(f"{YELLOW}No smoke test directory found at {SMOKE_DIR}{NC}")
        return

    cmm_files = sorted(SMOKE_DIR.glob("*.cmm"))
    if not cmm_files:
        print(f"{YELLOW}No .cmm files in {SMOKE_DIR}{NC}")
        return

    print(f"\n{BOLD}Smoke Tests ({len(cmm_files)} files){NORMAL}")
    print("-" * 50)

    workdir = SCRIPT_DIR / "workdir"
    passed = 0
    for cmm in cmm_files:
        name = cmm.stem
        s_file = workdir / f"smoke_{name}.s"
        ir_file = workdir / f"smoke_{name}.ir"

        for f in (s_file, ir_file):
            if f.exists():
                f.unlink()

        try:
            proc = subprocess.run(
                [parser_path, str(cmm), str(s_file), str(ir_file)],
                timeout=timeout,
                capture_output=True,
                text=True
            )
        except subprocess.TimeoutExpired:
            print(f"  {RED}FAIL{NC} {name}.cmm: TLE (compilation)")
            continue

        if proc.returncode != 0:
            print(f"  {RED}FAIL{NC} {name}.cmm: compilation error")
            continue

        if not s_file.exists():
            print(f"  {RED}FAIL{NC} {name}.cmm: no .s output")
            continue

        # Quick spim check: just see if it runs without error
        try:
            proc = subprocess.run(
                ["spim", "-lstack", "1048576", "-stext", "1048576",
                 "-file", str(s_file)],
                input="0\n",  # dummy input (read() returns 0)
                capture_output=True,
                text=True,
                timeout=timeout
            )
            if proc.returncode == 0:
                print(f"  {GREEN}OK{NC}   {name}.cmm: compiles and runs")
                passed += 1
            else:
                print(f"  {RED}FAIL{NC} {name}.cmm: spim error")
        except subprocess.TimeoutExpired:
            print(f"  {RED}FAIL{NC} {name}.cmm: TLE (spim)")

    print(f"\nSmoke: {GREEN}{passed}{NC}/{len(cmm_files)} passed")


def main():
    global args
    args = parse_args()

    # Validate parser path
    parser_path = Path(args.run).resolve()
    if not parser_path.exists() or not os.access(parser_path, os.X_OK):
        print(f"{RED}Error: parser '{args.run}' is not executable{NC}")
        sys.exit(1)

    # Smoke-only mode
    if args.smoke:
        workdir = SCRIPT_DIR / "workdir"
        workdir.mkdir(exist_ok=True)
        run_smoke_tests(str(parser_path), args.timeout)
        return

    # Ensure irsim is built
    print("Making (Updating) irsim...")
    build_irsim()

    workdir = SCRIPT_DIR / "workdir"
    workdir.mkdir(exist_ok=True)

    # Test discovery
    tests_dict = {
        'base': get_tests('base'),
        'advance': get_tests('advance'),
        'extend1': get_tests('extend/1'),
        'extend2': get_tests('extend/2'),
        'both': get_tests('extend/both'),
    }

    should_pass = []
    add_tests(should_pass, tests_dict['base'])

    if args.advance:
        add_tests(should_pass, tests_dict['advance'])

    should_fail = []

    if args.extend == 'extend1':
        add_tests(should_pass, tests_dict['extend1'])
        add_tests(should_fail, tests_dict['extend2'])
        add_tests(should_fail, tests_dict['both'])
    elif args.extend == 'extend2':
        add_tests(should_fail, tests_dict['extend1'])
        add_tests(should_pass, tests_dict['extend2'])
        add_tests(should_fail, tests_dict['both'])
    elif args.extend == 'both':
        add_tests(should_pass, tests_dict['extend1'])
        add_tests(should_pass, tests_dict['extend2'])
        add_tests(should_pass, tests_dict['both'])

    print(f"Tests to run: {len(should_pass)} pass + {len(should_fail)} fail "
          f"(extend={args.extend}, advance={args.advance}, prf={args.prf})")
    print(f"Timeout: {args.timeout}s, Jobs: {args.jobs}")
    print(f"irsim: {'ON' if not args.spim_only else 'OFF'}, "
          f"spim: {'ON' if not args.irsim_only else 'OFF'}")
    print("-" * 50)

    results = Results()

    with ThreadPoolExecutor(max_workers=args.jobs) as executor:
        futures = {}
        for t in should_pass:
            futures[executor.submit(
                run_single_test, t, True, str(parser_path), results)] = t
        for t in should_fail:
            futures[executor.submit(
                run_single_test, t, False, str(parser_path), results)] = t

        for future in as_completed(futures):
            t = futures[future]
            rel_path = t.relative_to(LAB3_TESTS)
            try:
                future.result()
            except Exception as e:
                results.add(rel_path, False, f"INTERNAL ERROR: {e}")

    # Print results sorted
    for rel_path, passed, msg, inst in sorted(
            results.details, key=lambda x: (x[1], x[0])):
        if passed:
            print(f"{GREEN}OK{NORMAL}   [{rel_path}] {msg}")
        else:
            print(f"{RED}{BOLD}FAIL{NORMAL} [{rel_path}] {msg}")
            if not args.continue_on_error:
                print(f"{RED}Exiting due to error. "
                      f"Run with -c to continue on error.{NC}")
                os._exit(1)

    results.summary()

    # Also run smoke tests if they exist
    smoke_files = list(SMOKE_DIR.glob("*.cmm")) if SMOKE_DIR.exists() else []
    if smoke_files:
        run_smoke_tests(str(parser_path), args.timeout)


if __name__ == "__main__":
    main()