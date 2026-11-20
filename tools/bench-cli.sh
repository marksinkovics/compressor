#!/bin/bash
# Time ingot encode and decode end to end (file I/O included) on corpus files.
#
#   tools/bench-cli.sh [--runs <n>] [--out <file>] [<file>...]
#
#   --runs   timed runs per command (default: 10)
#   --out    JSON output file (default: build/release/bench-cli.json)
#   <file>   inputs, relative to the repo root (default: DEFAULT_FILES from corpus/)
#
# Uses hyperfine (2.x) when it is installed; otherwise a python3 loop that writes the subset of
# hyperfine's JSON read here: results[].command and results[].summary.time_wall_clock.

set -euo pipefail

SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd -- "$SCRIPT_DIR/.." && pwd)"
BUILD_DIR="$REPO_ROOT/build/release"
APP=build/release/ingot/ingot
DEFAULT_FILES=(
    corpus/canterbury/kennedy.xls
    corpus/silesia/webster
    corpus/silesia/mozilla
)

fail() {
    printf 'ERROR: %s\n' "$1" >&2
    exit 1
}

time_with_python() {
    local runs="$1" out="$2"
    shift 2
    python3 -I - "$runs" "$out" "$@" << 'EOF'
import json, statistics, subprocess, sys, time

runs, out, commands = int(sys.argv[1]), sys.argv[2], sys.argv[3:]
results = []
for command in commands:
    subprocess.run(command, shell=True, check=True)  # warm-up
    times = []
    for _ in range(runs):
        start = time.perf_counter()
        subprocess.run(command, shell=True, check=True)
        times.append(time.perf_counter() - start)
    results.append({
        "command": command,
        "summary": {
            "time_wall_clock": {
                "unit": "second",
                "count": len(times),
                "mean": statistics.mean(times),
                "stddev": statistics.stdev(times) if len(times) > 1 else 0.0,
                "median": statistics.median(times),
                "min": min(times),
                "max": max(times),
            }
        },
    })
with open(out, "w", encoding="utf-8") as file:
    json.dump({"results": results}, file, indent=2)
EOF
}

print_summary() {
    python3 -I - "$1" << 'EOF'
import json, shlex, sys

with open(sys.argv[1], encoding="utf-8") as file:
    results = json.load(file)["results"]
print(f"{'command':<8} {'input':<40} {'mean s':>8} {'min s':>8}")
for result in results:
    words = shlex.split(result["command"])
    wall = result["summary"]["time_wall_clock"]
    print(f"{words[1]:<8} {words[3]:<40} {wall['mean']:>8.3f} {wall['min']:>8.3f}")
EOF
}

main() {
    local runs=10 out="$BUILD_DIR/bench-cli.json"
    local files=()
    while [[ $# -gt 0 ]]; do
        case "$1" in
            --runs)
                [[ $# -ge 2 ]] || fail "--runs needs a number"
                runs="$2"
                shift
                ;;
            --out)
                [[ $# -ge 2 ]] || fail "--out needs a file"
                out="$2"
                shift
                ;;
            -*) fail "unknown argument: $1" ;;
            *) files+=("$1") ;;
        esac
        shift
    done

    cd "$REPO_ROOT"
    [[ ${#files[@]} -gt 0 ]] || files=("${DEFAULT_FILES[@]}")
    [[ -f "$BUILD_DIR/CMakeCache.txt" ]] || cmake --preset release
    cmake --build --preset release --target ingot

    local tmp=build/release/bench-cli
    mkdir -p "$tmp"
    # shellcheck disable=SC2064 # expand $tmp now
    trap "rm -rf '$tmp'" EXIT

    local commands=() file name
    for file in "${files[@]}"; do
        [[ -f "$file" ]] || fail "missing input: $file (see tools/fetch-corpus.sh)"
        name="$(basename -- "$file")"
        "$APP" encode -i "$file" -o "$tmp/$name.huf"
        "$APP" decode -i "$tmp/$name.huf" -o "$tmp/$name.out"
        cmp -s "$file" "$tmp/$name.out" || fail "round trip differs: $file"
        commands+=("'$APP' encode -i '$file' -o '$tmp/$name.huf'")
        commands+=("'$APP' decode -i '$tmp/$name.huf' -o '$tmp/$name.out'")
    done

    mkdir -p "$(dirname -- "$out")"
    if command -v hyperfine > /dev/null 2>&1; then
        hyperfine --warmup 1 --runs "$runs" --export-json "$out" "${commands[@]}"
    else
        printf 'hyperfine not found; timing with python3\n' >&2
        time_with_python "$runs" "$out" "${commands[@]}"
    fi
    print_summary "$out"
    printf 'Results: %s\n' "$out"
}

main "$@"
