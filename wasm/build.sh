#!/bin/sh
# Rebuild the browser demo in docs/ (requires emsdk on PATH).
set -e
cd "$(dirname "$0")/.."
emcc -O2 -Isrc src/html.c src/css.c src/render.c wasm/ferret_wasm.c -o docs/ferret.js \
    -sEXPORTED_FUNCTIONS=_ferret_render \
    -sEXPORTED_RUNTIME_METHODS=ccall,cwrap \
    -sMODULARIZE=1 -sEXPORT_NAME=FerretModule \
    -sSTACK_SIZE=4194304 -sALLOW_MEMORY_GROWTH=1
