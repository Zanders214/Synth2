#!/usr/bin/env python3
"""Paired benchmark gate for pull requests.

perf.yml builds perf_bench twice on one runner, from the PR's base and from its
head, and runs them in turns (base, head, base, head, ...). Each round leaves a
pair of bench_result.json files. For every ns/block series this takes the
head/base ratio within each round and gates on the median of those ratios, so a
slow or noisy runner slows both builds alike and cancels out.

usage: bench_gate.py RESULTS_DIR   (holds base-1.json, head-1.json, base-2.json, ...)
env:   BENCH_FAIL_RATIO (default 1.15), BENCH_WARN_RATIO (default 1.07)
Writes a Markdown table to $GITHUB_STEP_SUMMARY when set; exits 1 on a regression.
"""

import json
import os
import statistics
import sys
from pathlib import Path


def load(path):
    # Only the timings: the "DSP load %" series are the same numbers over a constant.
    return {e["name"]: float(e["value"]) for e in json.loads(path.read_text()) if e.get("unit") == "ns/block"}


def main():
    results = Path(sys.argv[1])
    fail_ratio = float(os.environ.get("BENCH_FAIL_RATIO", "1.15"))
    warn_ratio = float(os.environ.get("BENCH_WARN_RATIO", "1.07"))

    rounds = []
    n = 1
    while (results / f"base-{n}.json").exists() and (results / f"head-{n}.json").exists():
        rounds.append((load(results / f"base-{n}.json"), load(results / f"head-{n}.json")))
        n += 1
    if not rounds:
        print(f"no base-N.json / head-N.json pairs in {results}", file=sys.stderr)
        return 2

    names = list(rounds[0][1])                          # the head's series, in its order
    gone = [s for s in rounds[0][0] if s not in names]  # series the PR removed

    rows, failed = [], []
    for name in names:
        heads = [h[name] for _, h in rounds]
        if name not in rounds[0][0]:
            rows.append((name, None, statistics.median(heads), None, "new, no baseline"))
            continue
        bases = [b[name] for b, _ in rounds]
        ratio = statistics.median(h / b for b, h in zip(bases, heads))
        if ratio > fail_ratio:
            verdict = "**regression**"
            failed.append(name)
            print(f"::error title=perf regression::{name}: head/base {ratio:.3f} > {fail_ratio:.2f}")
        elif ratio > warn_ratio:
            verdict = "slower"
            print(f"::warning title=perf::{name}: head/base {ratio:.3f}")
        else:
            verdict = "ok"
        rows.append((name, statistics.median(bases), statistics.median(heads), ratio, verdict))

    def ns(v):
        return "–" if v is None else f"{v:,.0f}"

    lines = [
        f"### DSP benchmark: head vs base, {len(rounds)} interleaved rounds on one runner",
        "",
        "| Series | base ns/block | head ns/block | head / base | |",
        "|---|---:|---:|---:|---|",
    ]
    for name, base, head, ratio, verdict in rows:
        lines.append(f"| {name} | {ns(base)} | {ns(head)} | {'–' if ratio is None else f'{ratio:.3f}'} | {verdict} |")
    for name in gone:
        lines.append(f"| {name} | | | | removed |")
    lines += ["", f"Medians over the rounds; head / base is the median of each round's ratio. "
                  f"Fails above {fail_ratio:.2f}, warns above {warn_ratio:.2f}."]
    report = "\n".join(lines) + "\n"
    print(report)
    if summary := os.environ.get("GITHUB_STEP_SUMMARY"):
        with open(summary, "a") as f:
            f.write(report)

    if failed:
        print(f"{len(failed)} series slower than {fail_ratio:.2f}x the base: {', '.join(failed)}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
