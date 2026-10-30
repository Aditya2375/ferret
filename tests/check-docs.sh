#!/bin/sh
# Doc-claims guard: the unit-check counts in README.md must match what
# the suites actually print. Fails the build when a number drifts.
set -e
cd "$(dirname "$0")/.."
OUT=build/docs-check
mkdir -p "$OUT"

cc -std=gnu11 -O2 -g -Wall -Wextra -Werror -Isrc \
    tests/unit/test_html.c src/html.c -o "$OUT/test_html"
cc -std=gnu11 -O2 -g -Wall -Wextra -Werror -Isrc \
    tests/unit/test_css.c src/html.c src/css.c -o "$OUT/test_css"

HTML_N=$("$OUT/test_html" | grep -o '^[0-9]* checks passed' | grep -o '^[0-9]*')
CSS_N=$("$OUT/test_css" | grep -o '^[0-9]* checks passed' | grep -o '^[0-9]*')
TOTAL=$((HTML_N + CSS_N))
STATED=$(grep -o '([0-9]* checks)' README.md | head -1 | grep -o '[0-9]*')
if [ "$TOTAL" != "$STATED" ]; then
    echo "DOC DRIFT: README says $STATED unit checks, suites run $TOTAL ($HTML_N html + $CSS_N css)"
    exit 1
fi
echo "doc counts verified: $STATED checks"
