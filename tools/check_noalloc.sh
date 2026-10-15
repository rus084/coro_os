#!/usr/bin/env bash
set -euo pipefail
root="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
cxx="${NOALLOC_CXX:-}"
if [[ -z "$cxx" ]]; then
  echo 'SKIP: set NOALLOC_CXX to a compiler supporting coro::noalloc and frame builtins'; exit 77
fi
if ! command -v "$cxx" >/dev/null; then
  echo "FAIL: configured NOALLOC_CXX is unavailable: $cxx"; exit 1
fi
cross=false
if [[ "${1:-}" == --cross ]]; then cross=true; shift; fi
out="$root/build-noalloc"
mkdir -p "$out"
if ! "$cxx" -std=c++20 -fno-exceptions -fno-rtti -I"$root/include" "$@" -fsyntax-only \
    "$root/tests/noalloc_contract.cpp" >"$out/contract.log" 2>&1; then
  cat "$out/contract.log"
  echo 'FAIL: coro::task<void> contract requires noexcept move/resume (void or bool), const noexcept bool done() and noexcept bind_context(void*)'; exit 1
fi
for mode in REFERENCE_RESULT CONST_RESULT VOLATILE_RESULT ARRAY_RESULT THROWING_MOVE THROWING_DESTRUCTOR THROWING_CONVERSION; do
  if "$cxx" -std=c++20 -fno-exceptions -fno-rtti -I"$root/include" "$@" \
      -D"$mode" -fsyntax-only "$root/tests/noalloc_value_invalid.cpp" >"$out/invalid-$mode.log" 2>&1; then
    echo "FAIL: invalid task result accepted: $mode"; exit 1
  fi
  if ! grep -Eq 'coro::task<T>|co_return must construct' "$out/invalid-$mode.log"; then
    cat "$out/invalid-$mode.log"
    echo "FAIL: unexpected diagnostic for task result: $mode"; exit 1
  fi
done
echo 'PASS: invalid task result types and throwing construction rejected'
nm_tool="${NOALLOC_NM:-nm}"
if ! command -v "$nm_tool" >/dev/null; then
  echo "FAIL: symbol checker unavailable: $nm_tool"; exit 1
fi
for opt in 0 2 s; do
  for fixture in integration nested frame_slot value sync; do
    elf="$out/$fixture-O$opt.elf"
    if ! "$cxx" -std=c++20 "-O$opt" -fno-exceptions -fno-rtti -Wall -Wextra -Werror \
        -I"$root/include" "$@" "$root/tests/noalloc_$fixture.cpp" \
        -o "$elf" >"$out/$fixture-O$opt.log" 2>&1; then
      cat "$out/$fixture-O$opt.log"
      echo "FAIL: noalloc $fixture compilation/link at -O$opt"; exit 1
    fi
    "$nm_tool" -C "$elf" >"$elf.symbols"
    if grep -E 'operator new|operator delete|(^|[[:space:]])(malloc|calloc|realloc|free|__atomic_[[:alnum:]_]*|__cxa_allocate_exception|_Unwind_[[:alnum:]_]*)($|[[:space:]@])' "$elf.symbols"; then
      echo "FAIL: forbidden allocator/runtime symbol at -O$opt"; exit 1
    fi
    if "$cross"; then
      echo "PASS: noalloc $fixture -O$opt cross compile/link and symbol check (not executed)"
    else
      "$elf"
      echo "PASS: noalloc $fixture -O$opt host execution, frame reuse/cancellation and symbols"
    fi
  done
done
