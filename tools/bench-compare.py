#!/usr/bin/env python3
"""Compare two compressor_benchmark JSON results: speed and ratio per benchmark.

    tools/bench-compare.py <old.json> <new.json>
"""

import json
import sys


def load(path):
    with open(path, encoding="utf-8") as file:
        data = json.load(file)
    results = {}
    for run in data["benchmarks"]:
        if run.get("run_type", "iteration") != "iteration":
            continue
        results[run["name"]] = run
    return results


def mb_per_s(run):
    value = run.get("bytes_per_second")
    return None if value is None else value / (1024 * 1024)


def fmt(value, spec):
    width = spec.split(".")[0]
    return format("-", f">{width}") if value is None else format(value, spec)


def main(argv):
    if len(argv) != 3:
        print(__doc__.strip(), file=sys.stderr)
        return 2
    old, new = load(argv[1]), load(argv[2])
    names = [name for name in new if name in old]
    if not names:
        print("No benchmarks in common.", file=sys.stderr)
        return 1

    width = max(len(name) for name in names)
    print(f"{'Benchmark':<{width}}  {'old MB/s':>10}  {'new MB/s':>10}  {'speedup':>8}"
          f"  {'old ratio':>9}  {'new ratio':>9}")
    for name in names:
        before, after = old[name], new[name]
        if before.get("error_occurred") or after.get("error_occurred"):
            message = after.get("error_message") or before.get("error_message")
            print(f"{name:<{width}}  error: {message}")
            continue
        old_speed, new_speed = mb_per_s(before), mb_per_s(after)
        speedup = new_speed / old_speed if old_speed and new_speed else None
        print(f"{name:<{width}}  {fmt(old_speed, '10.2f')}  {fmt(new_speed, '10.2f')}"
              f"  {fmt(speedup, '7.2f')}{'x' if speedup else ' '}"
              f"  {fmt(before.get('ratio'), '9.4f')}  {fmt(after.get('ratio'), '9.4f')}")

    only_old = sorted(set(old) - set(new))
    only_new = sorted(set(new) - set(old))
    if only_old:
        print(f"\nOnly in {argv[1]}: {len(only_old)} benchmarks")
    if only_new:
        print(f"Only in {argv[2]}: {len(only_new)} benchmarks")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
