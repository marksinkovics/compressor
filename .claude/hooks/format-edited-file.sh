#!/bin/bash
# Claude Code PostToolUse hook: formats the file an Edit or Write just changed through
# tools/quality.sh, so formatting never shows up as a lint failure. It never blocks the edit.

set -uo pipefail

REPO_ROOT="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/../.." && pwd)"

file="$(python3 -c 'import json, sys; print(json.load(sys.stdin).get("tool_input", {}).get("file_path", ""))' 2> /dev/null)"
[[ -n "$file" && -f "$file" ]] || exit 0
case "$file" in
    "$REPO_ROOT"/*) ;;
    *) exit 0 ;;
esac
case "$file" in
    *.cpp | *.h | *.hpp | *.sh) ;;
    *) exit 0 ;;
esac

bash "$REPO_ROOT/tools/quality.sh" format -- "$file" > /dev/null 2>&1
exit 0
