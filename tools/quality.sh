#!/bin/bash
# Format and lint checks, one stage per tool.
#
#   tools/quality.sh check [stage...]   every stage, or only the named ones
#   tools/quality.sh check [stage...] -- file...
#                                       clang-tidy only on the sources those files affect
#   tools/quality.sh --fix              apply fixes, then run every stage
#   tools/quality.sh format             apply formatting only
#   tools/quality.sh stages             list the stage names
#
# Running more than one stage prints one line per stage with its elapsed time; a single stage
# (as each pre-commit hook runs) prints only its diagnostics.

set -euo pipefail

SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd -- "$SCRIPT_DIR/.." && pwd)"
BUILD_DIR="$REPO_ROOT/build/debug"

# Homebrew's llvm is keg-only and also ships a clang-format of a different version than the
# pinned one, so only its clang-tidy is taken, by full path.
CLANG_TIDY=clang-tidy
if command -v brew > /dev/null 2>&1; then
    LLVM_BIN="$(brew --prefix llvm 2> /dev/null)/bin"
    if [[ -x "$LLVM_BIN/clang-tidy" ]]; then
        CLANG_TIDY="$LLVM_BIN/clang-tidy"
    fi
fi

CLANG_FORMAT_MAJOR=20

STAGES=(clang-format clang-tidy shellcheck shfmt)

fail() {
    printf 'ERROR: %s\n' "$1" >&2
    exit 1
}

require_tools() {
    local missing=()
    local tool
    for tool in "$@"; do
        command -v "$tool" > /dev/null 2>&1 || missing+=("$tool")
    done

    if ((${#missing[@]} > 0)); then
        fail "missing tools: ${missing[*]}"
    fi
    for tool in "$@"; do
        [[ "$tool" == clang-format ]] && require_clang_format_version
    done
    return 0
}

# Each clang-format major version formats a little differently, so every machine and CI must use
# the same one.
require_clang_format_version() {
    local version pattern='version ([0-9]+)\.'
    version="$(clang-format --version)"
    [[ "$version" =~ $pattern ]] || fail "unrecognized clang-format version: $version"
    if [[ "${BASH_REMATCH[1]}" != "$CLANG_FORMAT_MAJOR" ]]; then
        fail "clang-format $CLANG_FORMAT_MAJOR is required, found: $version"
    fi
}

# macOS ships bash 3.2, which has no EPOCHREALTIME.
now() {
    if [[ -n "${EPOCHREALTIME:-}" ]]; then
        printf '%s\n' "${EPOCHREALTIME/,/.}"
    else
        python3 -c 'import time; print(time.time())'
    fi
}

elapsed_since() {
    awk -v start="$1" -v end="$(now)" 'BEGIN { printf "%.1fs", end - start }'
}

# Tracked and new, not ignored, files matching the patterns, as absolute paths separated by NUL.
# Files deleted from the working tree but still in the index are left out.
repo_files() {
    local file
    git -C "$REPO_ROOT" ls-files -z --cached --others --exclude-standard -- "$@" \
        | while IFS= read -r -d '' file; do
            [[ -f "$REPO_ROOT/$file" ]] && printf '%s\0' "$REPO_ROOT/$file"
        done
}

cpp_files() {
    repo_files '*.cpp' '*.h' '*.hpp'
}

shell_files() {
    repo_files '*.sh'
}

# shfmt takes its options from .editorconfig.
format_sources() {
    cpp_files | xargs -0 clang-format -i
    shell_files | xargs -0 shfmt -w
}

check_clang_format() {
    cpp_files | xargs -0 clang-format --dry-run --Werror
}

# Each clang-tidy run compiles its file, so files are checked in parallel.
job_count() {
    if command -v sysctl > /dev/null 2>&1 && sysctl -n hw.ncpu > /dev/null 2>&1; then
        sysctl -n hw.ncpu
    elif command -v nproc > /dev/null 2>&1; then
        nproc
    else
        echo 4
    fi
}

# Our sources in the compile database, NUL-separated; googletest and benchmark are compiled from
# build/. Test sources appear twice there (library and _app), so the list is deduplicated.
tidy_sources() {
    grep -o '"file": "[^"]*"' "$BUILD_DIR/compile_commands.json" \
        | sed -e 's/^"file": "//' -e 's/"$//' \
        | grep "^$REPO_ROOT/" \
        | grep -v "^$REPO_ROOT/build/" \
        | sort -u \
        | tr '\n' '\0'
}

# The sources among tidy_sources that the given files can change the diagnostics of, one per
# line: the files themselves and every source whose last build included one of them, from
# Ninja's dependency log. Any other file (.clang-tidy, CMake files), or no usable log, selects
# everything.
affected_tidy_sources() {
    local file deps changed=""
    for file in "$@"; do
        case "$file" in
            *.cpp | *.h | *.hpp) changed+="$REPO_ROOT/$file"$'\n' ;;
            *)
                tidy_sources | tr '\0' '\n'
                return
                ;;
        esac
    done
    if ! deps="$(ninja -C "$BUILD_DIR" -t deps 2> /dev/null)" \
        || [[ "$deps" != *"$REPO_ROOT/compressor/"* ]]; then
        tidy_sources | tr '\0' '\n'
        return
    fi
    # Each record is an unindented object line, then its source and headers indented.
    awk '
        FNR == 1 { file++ }
        file == 1 { changed[$0] = 1; next }
        file == 2 {
            if ($0 !~ /^ /) { source = ""; next }
            sub(/^ +/, "")
            if (source == "") source = $0
            if ($0 in changed) hit[source] = 1
            next
        }
        ($0 in changed) || ($0 in hit)
    ' <(printf '%s' "$changed") <(printf '%s\n' "$deps") <(tidy_sources | tr '\0' '\n')
}

# clang-tidy writes "[n/N] Processing file" progress to stderr unbuffered, which garbles the
# output of parallel runs, so each run's output is collected, filtered and written at once.
tidy_batch() {
    local output status=0
    output="$("$CLANG_TIDY" --quiet -p "$BUILD_DIR" "$@" 2>&1)" || status=$?
    output="$(grep -v -E '^\[[0-9]+/[0-9]+\]( \([0-9]+/[0-9]+\))? Processing file ' \
        <<< "$output" || true)"
    [[ -z "$output" ]] || printf '%s\n' "$output"
    return "$status"
}

# Reads NUL-separated sources on standard input.
run_tidy_parallel() {
    export -f tidy_batch
    export CLANG_TIDY BUILD_DIR
    xargs -0 -n 4 -P "$(job_count)" "$BASH" -c 'tidy_batch "$@"' tidy_batch
}

# With files, checks only the sources they affect and says how many on descriptor 3, which
# check_stages keeps on its standard output.
check_clang_tidy() {
    local selected count total status=0
    if (($# == 0)); then
        tidy_sources | run_tidy_parallel
        return
    fi
    selected="$(mktemp)"
    affected_tidy_sources "$@" > "$selected"
    count="$(wc -l < "$selected" | tr -d ' ')"
    total="$(tidy_sources | tr -cd '\0' | wc -c | tr -d ' ')"
    printf 'clang-tidy: %s of %s sources affected\n' "$count" "$total" >&3
    if ((count > 0)); then
        tr '\n' '\0' < "$selected" | run_tidy_parallel || status=$?
    fi
    rm -f "$selected"
    return "$status"
}

# Fixing stays serial: parallel runs fixing the same header would overwrite each other's edits.
# run-clang-tidy avoids that, but it also applies the fixes clang-tidy only suggests in notes.
# Warnings aren't errors here, so a run that fixes everything succeeds; the check after it
# reports what is left.
fix_clang_tidy() {
    tidy_sources | xargs -0 "$CLANG_TIDY" --quiet --fix --warnings-as-errors='-*' -p "$BUILD_DIR"
}

is_built() {
    [[ -f "$BUILD_DIR/compile_commands.json" ]]
}

stage_label() {
    case "$1" in
        shellcheck) printf 'ShellCheck' ;;
        shfmt) printf 'shfmt check' ;;
        *) printf '%s' "$1" ;;
    esac
}

stage_tool() {
    case "$1" in
        clang-tidy) echo "$CLANG_TIDY" ;;
        *) echo "$1" ;;
    esac
}

run_stage() {
    local stage="$1"
    shift
    case "$stage" in
        clang-format) check_clang_format ;;
        clang-tidy) check_clang_tidy "$@" ;;
        shellcheck) shell_files | xargs -0 shellcheck -x ;;
        shfmt) shell_files | xargs -0 shfmt -d ;;
    esac
}

is_stage() {
    local stage
    for stage in "${STAGES[@]}"; do
        [[ "$stage" == "$1" ]] && return 0
    done
    return 1
}

# Runs a fix step with its output hidden unless it fails.
run_fix() {
    local label="$1"
    local output_file
    shift

    output_file="$(mktemp)"
    if "$@" > "$output_file" 2>&1; then
        rm -f "$output_file"
        return 0
    fi
    printf 'ERROR: %s failed\n' "$label" >&2
    cat "$output_file" >&2
    rm -f "$output_file"
    return 1
}

apply_fixes() {
    run_fix "format fixes" format_sources
    if is_built; then
        run_fix "clang-tidy fixes" fix_clang_tidy
    fi
}

# Arguments are stages, then optionally `--` and the files that changed, which narrow the
# clang-tidy stage. Runs every stage, even after a failure, so one run reports all of them.
check_stages() {
    local stages=()
    local files=()
    while (($# > 0)) && [[ "$1" != "--" ]]; do
        stages+=("$1")
        shift
    done
    if (($# > 0)); then
        shift
        files=("$@")
        ((${#files[@]} > 0)) || return 0
    fi
    ((${#stages[@]} > 0)) || stages=("${STAGES[@]}")
    local summary=false
    local failed=0
    local total_start stage label start output_file tools

    ((${#stages[@]} > 1)) && summary=true

    tools=()
    for stage in "${stages[@]}"; do
        is_stage "$stage" || fail "unknown stage '$stage' (stages: ${STAGES[*]})"
        tools+=("$(stage_tool "$stage")")
        if [[ "$stage" == clang-tidy ]] && ! is_built; then
            fail "no compile database in ${BUILD_DIR#"$REPO_ROOT/"}; run './generate.sh' first"
        fi
    done
    require_tools "${tools[@]}"

    total_start="$(now)"
    for stage in "${stages[@]}"; do
        label="$(stage_label "$stage")"
        start="$(now)"
        output_file="$(mktemp)"
        if run_stage "$stage" ${files[@]+"${files[@]}"} 3>&1 > "$output_file" 2>&1; then
            if [[ "$summary" == true ]]; then
                printf 'ok    %-14s %s\n' "$label" "$(elapsed_since "$start")"
            fi
        else
            failed=1
            if [[ "$summary" == true ]]; then
                printf 'FAIL  %-14s %s\n' "$label" "$(elapsed_since "$start")"
            fi
            printf 'ERROR: %s failed\n' "$label" >&2
            cat "$output_file" >&2
        fi
        rm -f "$output_file"
    done
    if [[ "$summary" == true ]]; then
        printf '%-20s %s\n' "total" "$(elapsed_since "$total_start")"
    fi
    return "$failed"
}

case "${1:-}" in
    format)
        require_tools clang-format shfmt
        format_sources
        ;;
    check)
        shift
        check_stages "$@"
        ;;
    --fix)
        require_tools clang-format "$CLANG_TIDY" shellcheck shfmt
        apply_fixes
        check_stages "${STAGES[@]}"
        ;;
    stages)
        printf '%s\n' "${STAGES[@]}"
        ;;
    *)
        fail "usage: $0 {check [stage...] [-- file...]|--fix|format|stages}"
        ;;
esac
