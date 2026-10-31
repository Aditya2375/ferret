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

# redirect handling against a local server: relative, absolute-path and
# absolute-URL hops must chain, and a redirect loop must stay bounded
if command -v python3 >/dev/null; then
    python3 tests/redirect_server.py &
    SRV=$!
    trap 'kill $SRV 2>/dev/null' EXIT
    sleep 1
    ./build/ferret http://127.0.0.1:8799/start --no-color > "$OUT/redirect.txt"
    grep -q "made it" "$OUT/redirect.txt" || { echo "redirect chain failed"; exit 1; }
    if ./build/ferret http://127.0.0.1:8799/loop1 > /dev/null 2>&1; then
        echo "redirect loop was not bounded"; exit 1
    fi
    # an unbounded response must hit the body-size limit, not eat memory
    if ./build/ferret http://127.0.0.1:8799/huge > /dev/null 2> "$OUT/huge.err"; then
        echo "unbounded response was accepted"; exit 1
    fi
    grep -q "exceeds the 8 MiB limit" "$OUT/huge.err" || { echo "wrong size-limit error:"; cat "$OUT/huge.err"; exit 1; }
    # a body cut off mid-stream must not render as a complete page
    if ./build/ferret http://127.0.0.1:8799/stall > /dev/null 2> "$OUT/stall.err"; then
        echo "stalled response rendered as complete"; exit 1
    fi
    grep -q "read from" "$OUT/stall.err" || { echo "wrong stall error:"; cat "$OUT/stall.err"; exit 1; }
    # a Location line inside the body is not a redirect: a 301 with no
    # real Location header is an error, never a jump to body text
    if ./build/ferret http://127.0.0.1:8799/fake-location > /dev/null 2> "$OUT/fakeloc.err"; then
        echo "301 without Location followed or rendered"; exit 1
    fi
    grep -q "HTTP 301" "$OUT/fakeloc.err" || { echo "wrong no-Location error:"; cat "$OUT/fakeloc.err"; exit 1; }
    # a directory is not an HTML file (old builds tried to malloc LONG_MAX)
    if ./build/ferret /tmp > /dev/null 2> "$OUT/dir.err"; then
        echo "directory accepted as input"; exit 1
    fi
    grep -q "not a regular file" "$OUT/dir.err" || { echo "wrong directory error:"; cat "$OUT/dir.err"; exit 1; }
    kill $SRV 2>/dev/null
    trap - EXIT
    echo "REDIRECT TESTS PASSED"

    # extreme nesting: styling is depth-capped, content must survive
    python3 -c "
d = 50000
open('$OUT/deep.html', 'w').write('<html><body>' + '<div>' * d + 'DEEP CONTENT' + '</div>' * d + '</body></html>')"
    timeout 60 ./build/ferret "$OUT/deep.html" --no-color | grep -q 'DEEP CONTENT' \
        || { echo "deep nesting crashed or lost content"; exit 1; }
    echo "DEEP RENDER PASSED"
fi
# TLS: only when this binary was built with make TLS=1
# a closed localhost port fails differently with and without TLS support
if ./build/ferret https://127.0.0.1:1/ 2>&1 | grep -q "without TLS support"; then
    echo "TLS TEST SKIPPED (binary built without TLS)"
else
    ./tests/tls_test.sh
fi

echo "ALL TESTS PASSED"
