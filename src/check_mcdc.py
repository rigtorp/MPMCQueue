#!/usr/bin/env python3
"""Verify MC/DC coverage of include/rigtorp/MPMCQueue.h using gcov -g."""

import argparse
import collections
import re
import subprocess
import sys
import tempfile
from pathlib import Path


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--gcno", type=Path, required=True, help="Path to MPMCQueueTest.cpp.gcno")
    parser.add_argument("--gcov", default="gcov", help="gcov executable")
    args = parser.parse_args()

    gcno = args.gcno.resolve()
    if not gcno.with_suffix(".gcda").is_file():
        parser.error(f"missing profile data for {gcno}; run MPMCQueueTest first")
    with tempfile.TemporaryDirectory(prefix="mpmc-mcdc-") as tmpdir:
        subprocess.run(
            [args.gcov, "-g", str(gcno)],
            cwd=tmpdir,
            stdout=subprocess.DEVNULL,
            check=True,
        )
        gcov_file = Path(tmpdir) / "MPMCQueue.h.gcov"
        if not gcov_file.exists():
            raise SystemExit(f"Missing {gcov_file}")
        lines = gcov_file.read_text().splitlines()

    src = {}
    exec_counts = collections.defaultdict(int)
    inst_conds = collections.defaultdict(list)
    cur_ln = None
    cur_inst = None

    for line in lines:
        match = re.match(r"^\s*([^:]+):\s*(\d+):(.*)$", line)
        if match and not line.startswith("condition"):
            cnt, ln, code = match.group(1).strip(), int(match.group(2)), match.group(3)
            cur_ln = ln
            src.setdefault(ln, code)
            if cnt not in ("-", "#####", "====="):
                exec_counts[ln] += int(cnt.rstrip("*"))
            cur_inst = None
        elif line.startswith("condition outcomes covered"):
            match2 = re.match(r"condition outcomes covered (\d+)/(\d+)", line)
            if match2 and cur_ln is not None:
                cur_inst = [int(match2.group(2)), set()]
                inst_conds[cur_ln].append(cur_inst)
        elif line.startswith("condition ") and cur_inst is not None:
            missing = re.fullmatch(r"condition\s+(\d+) not covered \((true|false|true false)\)", line)
            if not missing:
                raise SystemExit(f"Unexpected gcov output: {line}")
            for outcome in missing[2].split():
                cur_inst[1].add((int(missing[1]), outcome))

    never_exec = {
        int(m.group(1))
        for line in lines
        for m in [re.match(r"^\s*(?:#####|=====):\s*(\d+):", line)]
        if m and exec_counts[int(m.group(1))] == 0
    }

    gaps = {}
    total_outcomes = 0
    covered_outcomes = 0
    for ln, insts in sorted(inst_conds.items()):
        tot = max(t for t, _ in insts)
        common_uncov = set.intersection(*(u for _, u in insts))
        cov = tot - len(common_uncov)
        total_outcomes += tot
        covered_outcomes += cov
        if common_uncov:
            gaps[ln] = (cov, tot, src[ln].strip(), sorted(common_uncov))

    print(f"MPMCQueue.h MC/DC condition outcomes: {covered_outcomes}/{total_outcomes}")
    for ln in sorted(never_exec):
        print(f"UNEXECUTED L{ln}: {src[ln]}")
    for ln, (cov, tot, code, uncov) in gaps.items():
        print(f"CONDITION GAP L{ln} ({cov}/{tot}): {code} -> {uncov}")

    if not total_outcomes:
        print("No instrumented conditions found", file=sys.stderr)
    if not total_outcomes or never_exec or gaps:
        sys.exit(1)


if __name__ == "__main__":
    main()
