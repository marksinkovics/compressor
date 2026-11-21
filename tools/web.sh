#!/bin/bash
# Build, test or serve the WebAssembly demo (web/), compiled with Emscripten from the engine's
# sources.
#
#   tools/web.sh build   # build/web/: index.html, app.js, style.css, ingot_demo.{js,wasm}
#   tools/web.sh test    # build, then run web/test/test_demo.mjs with emsdk's node
#   tools/web.sh serve   # build, then serve build/web on http://localhost:8000
#
# Emscripten comes from emsdk in build/emsdk (git-ignored; docs/development.md, "WebAssembly
# demo"), or from EMSDK if that is set. Rebuilds only when a source is newer than the output.

set -euo pipefail

SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd -- "$SCRIPT_DIR/.." && pwd)"
OUT_DIR="$REPO_ROOT/build/web"
EMSDK_DIR="${EMSDK:-$REPO_ROOT/build/emsdk}"

# The engine and what it links; no threads, files or archives.
SOURCES=(
    web/src/demo.cpp
    compressor/src/compressor/bwt/bwt.cpp
    compressor/src/compressor/checksum/crc32.cpp
    compressor/src/compressor/cm/cm.cpp
    compressor/src/compressor/data/data.cpp
    compressor/src/compressor/deflate/deflate.cpp
    compressor/src/compressor/deflate/inflate.cpp
    compressor/src/compressor/dictionary/dictionary.cpp
    compressor/src/compressor/engine/engine.cpp
    compressor/src/compressor/entropy/rans.cpp
    compressor/src/compressor/entropy/tans.cpp
    compressor/src/compressor/error.cpp
    compressor/src/compressor/gpu/gpu.cpp
    compressor/src/compressor/huffman/huffman.cpp
    compressor/src/compressor/lz77/lz77.cpp
    compressor/src/compressor/lzr/lzr.cpp
    compressor/src/compressor/lzt/lzt.cpp
    compressor/src/compressor/thread/pool.cpp
    compressor/src/compressor/transform/transform.cpp
    compressor/src/compressor/tree/binary/node.cpp
    compressor/src/compressor/tree/binary/nodeiterator.cpp
    compressor/src/compressor/tree/binary/tree.cpp
    compressor/src/compressor/tree/symbolnode.cpp
)
PAGE_FILES=(index.html app.js style.css)

fail() {
    printf 'ERROR: %s\n' "$1" >&2
    exit 1
}

activate() {
    [[ -f "$EMSDK_DIR/emsdk_env.sh" ]] \
        || fail "no emsdk in $EMSDK_DIR; see docs/development.md, \"WebAssembly demo\""
    # shellcheck source=/dev/null
    source "$EMSDK_DIR/emsdk_env.sh" > /dev/null 2>&1
    command -v em++ > /dev/null || fail "em++ not found after activating $EMSDK_DIR"
}

stale() {
    local output="$OUT_DIR/ingot_demo.wasm"
    [[ -f "$output" ]] || return 0
    cd "$REPO_ROOT"
    [[ -n "$(find "${SOURCES[@]}" compressor/include web/src -newer "$output" -print -quit)" ]]
}

build() {
    activate
    mkdir -p "$OUT_DIR"
    for file in "${PAGE_FILES[@]}"; do
        cp "$REPO_ROOT/web/$file" "$OUT_DIR/$file"
    done
    if ! stale; then
        echo "build/web is up to date"
        return
    fi
    cd "$REPO_ROOT"
    em++ -std=c++23 -O2 -DPROFILING=0 -Wall -Wextra -Werror \
        -Icompressor/include -Icompressor/src -Ilib/instrumentor/include \
        "${SOURCES[@]}" \
        -lembind -sMODULARIZE=1 -sEXPORT_ES6=1 -sEXPORT_NAME=createIngotDemo \
        -sALLOW_MEMORY_GROWTH=1 -sENVIRONMENT=web,node \
        -o "$OUT_DIR/ingot_demo.js"
    echo "built $OUT_DIR"
}

case "${1:-build}" in
    build)
        build
        ;;
    test)
        build
        node "$REPO_ROOT/web/test/test_demo.mjs" "$OUT_DIR/ingot_demo.js"
        ;;
    serve)
        build
        echo "http://localhost:8000"
        python3 -m http.server 8000 --directory "$OUT_DIR"
        ;;
    *)
        fail "usage: tools/web.sh [build|test|serve]"
        ;;
esac
