#!/bin/sh
# ferret test suite: unit tests plus a golden end-to-end render.
set -e
cd "$(dirname "$0")/.."
OUT=build/tests
mkdir -p "$OUT"

cc -std=gnu11 -O2 -g -Wall -Wextra -Werror -Isrc \
    tests/unit/test_html.c src/html.c -o "$OUT/test_html"
"$OUT/test_html"

cc -std=gnu11 -O2 -g -Wall -Wextra -Werror -Isrc \
    tests/unit/test_css.c src/html.c src/css.c -o "$OUT/test_css"
"$OUT/test_css"

# end-to-end: render the sample page and check golden output
./build/ferret tests/page.html --no-color > "$OUT/got.txt"
diff -u tests/page.expected "$OUT/got.txt"
echo "RENDER TEST PASSED"
echo "ALL TESTS PASSED"
