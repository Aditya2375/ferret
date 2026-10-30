#!/bin/sh
# TLS end-to-end: a self-signed server cert must be rejected by default
# and accepted when explicitly trusted via SSL_CERT_FILE.
set -e
cd "$(dirname "$0")/.."
OUT=build/tests
mkdir -p "$OUT"
command -v openssl >/dev/null || { echo "openssl missing, skipping TLS test"; exit 0; }
command -v python3 >/dev/null || { echo "python3 missing, skipping TLS test"; exit 0; }

openssl req -x509 -newkey rsa:2048 -keyout "$OUT/tls.pem" -out "$OUT/tls.pem" \
    -days 1 -nodes -subj "/CN=127.0.0.1" \
    -addext "basicConstraints=critical,CA:TRUE" \
    -addext "subjectAltName=IP:127.0.0.1" >/dev/null 2>&1

python3 tests/tls_server.py "$OUT/tls.pem" 8443 &
SRV=$!
trap 'kill $SRV 2>/dev/null' EXIT
sleep 1

# untrusted by default: must fail
if SSL_CERT_FILE=/dev/null ./build/ferret https://127.0.0.1:8443/ >/dev/null 2>&1; then
    echo "TLS TEST FAILED: self-signed certificate was accepted"; exit 1
fi
# explicitly trusted: must succeed and render
SSL_CERT_FILE="$OUT/tls.pem" ./build/ferret https://127.0.0.1:8443/ --no-color > "$OUT/tls.txt"
grep -q "secure page reached" "$OUT/tls.txt" || { echo "TLS TEST FAILED: trusted page not rendered"; exit 1; }
echo "TLS TEST PASSED"
