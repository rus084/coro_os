#!/usr/bin/env bash
set -euo pipefail
root="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
out="$root/build-script-tests"
mkdir -p "$out"
set +e
NOALLOC_CXX= bash "$root/tools/check_noalloc.sh" >"$out/unset.log" 2>&1
status=$?
set -e
test "$status" -eq 77
grep -q 'SKIP:' "$out/unset.log"
set +e
NOALLOC_CXX=/nonexistent/coro_os-compiler bash "$root/tools/check_noalloc.sh" >"$out/missing.log" 2>&1
status=$?
set -e
test "$status" -eq 1
grep -q 'FAIL:' "$out/missing.log"
# A configured incompatible compiler must fail. Do not assume that the user's
# build compiler lacks noalloc: it is intended to gain this very feature.
set +e
NOALLOC_CXX="$root/tests/support/unsupported_compiler.sh" bash "$root/tools/check_noalloc.sh" >"$out/unsupported.log" 2>&1
status=$?
set -e
test "$status" -eq 1
grep -q 'FAIL:' "$out/unsupported.log"
echo 'PASS: unset compiler -> SKIP; missing/unsupported configured compiler -> FAIL'
