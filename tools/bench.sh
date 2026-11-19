#!/bin/bash
# Build the release preset and run compressor_benchmark, writing JSON results.
#
#   tools/bench.sh [--filter <regex>] [--out <file>] [-- <benchmark args>...]
#
#   --filter    a --benchmark_filter regex (default: everything)
#   --out       JSON output file (default: build/release/benchmark.json)
#
# Compare two runs with tools/bench-compare.py <old.json> <new.json>.

set -euo pipefail

SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd -- "$SCRIPT_DIR/.." && pwd)"
BUILD_DIR="$REPO_ROOT/build/release"

fail() {
    printf 'ERROR: %s\n' "$1" >&2
    exit 1
}

main() {
    local filter="" out="$BUILD_DIR/benchmark.json"
    local extra=()
    while [[ $# -gt 0 ]]; do
        case "$1" in
            --filter)
                [[ $# -ge 2 ]] || fail "--filter needs a regex"
                filter="$2"
                shift
                ;;
            --out)
                [[ $# -ge 2 ]] || fail "--out needs a file"
                out="$2"
                shift
                ;;
            --)
                shift
                extra=("$@")
                break
                ;;
            *) fail "unknown argument: $1" ;;
        esac
        shift
    done

    cd "$REPO_ROOT"
    [[ -f "$BUILD_DIR/CMakeCache.txt" ]] || cmake --preset release
    cmake --build --preset release --target compressor_benchmark

    mkdir -p "$(dirname -- "$out")"
    local args=(--benchmark_out="$out" --benchmark_out_format=json)
    [[ -z "$filter" ]] || args+=(--benchmark_filter="$filter")
    # ${extra[@]+...}: bash 3.2 (macOS) treats an empty array as unset under set -u.
    "$BUILD_DIR/compressor_benchmark/compressor_benchmark" "${args[@]}" ${extra[@]+"${extra[@]}"}
    printf 'Results: %s\n' "$out"
}

main "$@"
