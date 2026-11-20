#!/usr/bin/env python3
"""Compare ingot with gzip, zstd, xz, bzip2 and brotli, and keep the results over time.

    python3 tools/compare.py [--corpus NAME...] [--dir PATH...] [--tools NAME...] [--threads N]
                             [--runs N] [--app PATH] [--no-save]

Each corpus (corpus/<name>, from tools/fetch-corpus.sh) is packed into one reproducible tar, so
every tool compresses the same stream. Every tool runs from the command line with its output going
to a file, and every round trip is checked. Results go to docs/benchmarks/history/ as JSON named
after the date and commit, and the ingot rows are compared with the newest earlier file.
"""

import argparse
import datetime
import hashlib
import io
import json
import os
import platform
import shutil
import subprocess
import sys
import tarfile
import time
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent
HISTORY = REPO / "docs" / "benchmarks" / "history"
WORK = REPO / "build" / "compare"
CORPORA = ["canterbury", "silesia", "images"]


def tools(app, threads):
    """Name -> (compress command, decompress command); {i} and {o} are the file paths."""
    t = str(threads)
    table = {}
    for level in (1, 6, 9):
        table[f"ingot -L {level}"] = (
            [app, "encode", "-L", str(level), "-T", t, "-i", "{i}", "-o", "{o}"],
            [app, "decode", "-T", t, "-i", "{i}", "-o", "{o}"],
        )
    for level in (6, 9):
        table[f"gzip -{level}"] = (["gzip", f"-{level}", "-c", "{i}"], ["gzip", "-d", "-c", "{i}"])
    for level in (3, 19):
        table[f"zstd -{level}"] = (
            ["zstd", "-q", f"-{level}", f"-T{t}", "-c", "{i}"],
            ["zstd", "-q", "-d", "-c", "{i}"],
        )
    for level in (6, 9):
        table[f"xz -{level}"] = (
            ["xz", f"-{level}", f"-T{t}", "-c", "{i}"],
            ["xz", "-d", f"-T{t}", "-c", "{i}"],
        )
    table["bzip2 -9"] = (["bzip2", "-9", "-c", "{i}"], ["bzip2", "-d", "-c", "{i}"])
    table["brotli -q 9"] = (
        ["brotli", "-q", "9", "-w", "24", "-c", "{i}"],
        ["brotli", "-d", "-c", "{i}"],
    )
    return table


def multithreaded(name):
    return name.split()[0] in ("ingot", "zstd", "xz")


def pack(corpus, source):
    """A tar of the corpus with sorted names and no times or owners, the same bytes every run."""
    if not source.is_dir():
        return None
    target = WORK / f"{corpus}.tar"
    buffer = io.BytesIO()
    with tarfile.open(fileobj=buffer, mode="w", format=tarfile.USTAR_FORMAT) as tar:
        for path in sorted(p for p in source.rglob("*") if p.is_file()):
            info = tarfile.TarInfo(str(path.relative_to(source)))
            info.size = path.stat().st_size
            info.mode = 0o644
            with path.open("rb") as stream:
                tar.addfile(info, stream)
    data = buffer.getvalue()
    if not target.exists() or target.read_bytes() != data:
        target.write_bytes(data)
    return target


def run(command, source, target):
    """Wall time of one run; the output goes to `target` (through stdout if no {o})."""
    args = [str(source) if a == "{i}" else str(target) if a == "{o}" else a for a in command]
    start = time.perf_counter()
    if "{o}" in command:
        subprocess.run(args, check=True, stdout=subprocess.DEVNULL)
    else:
        with target.open("wb") as out:
            subprocess.run(args, check=True, stdout=out)
    return time.perf_counter() - start


def same_file(a, b):
    def digest(path):
        h = hashlib.sha256()
        with path.open("rb") as stream:
            for chunk in iter(lambda: stream.read(1 << 20), b""):
                h.update(chunk)
        return h.digest()

    return a.stat().st_size == b.stat().st_size and digest(a) == digest(b)


def measure(name, commands, source, runs, threads):
    packed = WORK / f"{source.stem}.{name.replace(' ', '')}.out"
    back = WORK / f"{source.stem}.{name.replace(' ', '')}.back"
    for path in (packed, back):
        if path.exists():
            path.unlink()
    encode = min(run(commands[0], source, packed) for _ in range(runs))
    decode = min(run(commands[1], packed, back) for _ in range(runs))
    if not same_file(source, back):
        sys.exit(f"ERROR: {name} did not round-trip {source.name}")
    size = packed.stat().st_size
    packed.unlink()
    back.unlink()
    mb = source.stat().st_size / 1e6
    return {
        "tool": name,
        "threads": threads if multithreaded(name) else 1,
        "size": size,
        "ratio": round(source.stat().st_size / size, 4),
        "encode_s": round(encode, 4),
        "decode_s": round(decode, 4),
        "encode_mb_s": round(mb / encode, 2),
        "decode_mb_s": round(mb / decode, 2),
    }


def git(*args):
    return subprocess.run(
        ["git", *args], cwd=REPO, capture_output=True, text=True, check=False
    ).stdout.strip()


def previous_run(exclude):
    files = sorted(p for p in HISTORY.glob("*.json") if p != exclude)
    return json.loads(files[-1].read_text()) if files else None


def print_table(result, before):
    old = {}
    if before:
        for corpus, rows in before["corpora"].items():
            for row in rows["results"]:
                old[(corpus, row["tool"])] = row
    for corpus, entry in result["corpora"].items():
        print(f"\n{corpus}: {entry['bytes']:,} bytes")
        print(f"  {'tool':<14}{'size':>14}{'ratio':>8}{'enc MB/s':>10}{'dec MB/s':>10}  change")
        for row in entry["results"]:
            change = ""
            prior = old.get((corpus, row["tool"]))
            if prior and row["tool"].startswith("ingot"):
                size = (row["size"] / prior["size"] - 1) * 100
                enc = (row["encode_mb_s"] / prior["encode_mb_s"] - 1) * 100
                dec = (row["decode_mb_s"] / prior["decode_mb_s"] - 1) * 100
                change = f"size {size:+.2f}%, enc {enc:+.0f}%, dec {dec:+.0f}%"
            print(
                f"  {row['tool']:<14}{row['size']:>14,}{row['ratio']:>8.3f}"
                f"{row['encode_mb_s']:>10.1f}{row['decode_mb_s']:>10.1f}  {change}"
            )
    if before:
        print(f"\nchanges against {before['commit'][:9]} ({before['date']})")


def main():
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--corpus", nargs="*", default=CORPORA, choices=CORPORA)
    parser.add_argument("--dir", nargs="+", default=[], type=Path, help="other folders to pack")
    parser.add_argument("--tools", nargs="+", help="tool names to run, e.g. ingot xz (default: all)")
    parser.add_argument("--threads", type=int, default=6, help="for ingot, zstd and xz (default 6)")
    parser.add_argument("--runs", type=int, default=1, help="best of N timed runs (default 1)")
    parser.add_argument("--app", default=str(REPO / "build" / "release" / "ingot" / "ingot"))
    parser.add_argument("--no-save", action="store_true", help="print only, keep no history file")
    args = parser.parse_args()

    if not os.access(args.app, os.X_OK):
        sys.exit(f"ERROR: {args.app} not found; run cmake --workflow --preset release")
    table = tools(args.app, args.threads)
    if args.tools:
        table = {k: v for k, v in table.items() if k.split()[0] in args.tools}
    missing = sorted({k.split()[0] for k in table if shutil.which(table[k][0][0]) is None})
    if missing:
        sys.exit(f"ERROR: not installed: {', '.join(missing)}")
    WORK.mkdir(parents=True, exist_ok=True)

    commit = git("rev-parse", "HEAD")
    result = {
        "date": datetime.datetime.now().isoformat(timespec="seconds"),
        "commit": commit,
        "dirty": bool(git("status", "--porcelain", "--untracked-files=no")),
        "machine": f"{platform.system()} {platform.machine()}, {os.cpu_count()} cores",
        "threads": args.threads,
        "runs": args.runs,
        "versions": {
            name: subprocess.run(
                [name, "--version"], capture_output=True, text=True, check=False
            ).stdout.splitlines()[0:1]
            for name in ("zstd", "xz", "brotli")
            if shutil.which(name)
        },
        "corpora": {},
    }
    folders = [(name, REPO / "corpus" / name) for name in args.corpus]
    folders += [(path.resolve().name, path.resolve()) for path in args.dir]
    for corpus, folder in folders:
        source = pack(corpus, folder)
        if source is None:
            print(f"skipping {corpus}: run tools/fetch-corpus.sh {corpus}", file=sys.stderr)
            continue
        rows = []
        for name, commands in table.items():
            print(f"{corpus}: {name}", file=sys.stderr, flush=True)
            rows.append(measure(name, commands, source, args.runs, args.threads))
        result["corpora"][corpus] = {"bytes": source.stat().st_size, "results": rows}

    out = HISTORY / f"{result['date'][:10]}-{commit[:9]}.json"
    print_table(result, previous_run(out))
    if not args.no_save:
        HISTORY.mkdir(parents=True, exist_ok=True)
        out.write_text(json.dumps(result, indent=2) + "\n")
        print(f"\nwrote {out.relative_to(REPO)}")


if __name__ == "__main__":
    main()
