#!/usr/bin/env bash
set -euo pipefail
root="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
cxx="${ARM_CXX:-arm-none-eabi-g++}"
nm_tool="${ARM_NM:-arm-none-eabi-nm}"
dump="${ARM_OBJDUMP:-arm-none-eabi-objdump}"
if ! command -v "$cxx" >/dev/null || ! command -v "$nm_tool" >/dev/null || ! command -v "$dump" >/dev/null; then
  echo 'SKIP: Cortex-M compiler/nm/objdump unavailable'; exit 77
fi
out="$root/build-cortex-m"
mkdir -p "$out"
if ! printf '#include <coroutine>\n#include <type_traits>\n' | "$cxx" -mcpu=cortex-m0 -mthumb -std=c++20 -x c++ -fsyntax-only - >"$out/headers.log" 2>&1; then
  echo "SKIP: target C++20 headers unavailable; see $out/headers.log"; exit 77
fi
for cpu in cortex-m0 cortex-m3; do
  for opt in 0 2 s; do
    elf="$out/$cpu-O$opt.elf"
    "$cxx" -mcpu="$cpu" -mthumb -std=c++20 "-O$opt" -ffreestanding \
      -fno-exceptions -fno-rtti -fno-unwind-tables -fno-asynchronous-unwind-tables \
      -Wall -Wextra -Werror -nostdlib -I"$root/include" \
      "$root/tests/cortex_m_smoke.cpp" \
      -Wl,-T,"$root/tests/cortex_m_smoke.ld" -Wl,--no-undefined -lgcc -o "$elf"
    "$nm_tool" -C "$elf" >"$elf.symbols"
    if "$nm_tool" -u "$elf" | grep -q .; then
      echo "FAIL: unresolved symbols in $elf"; exit 1
    fi
    if grep -E 'operator new|operator delete|(^|[[:space:]])(malloc|calloc|realloc|free|__atomic_[[:alnum:]_]*|__cxa_[[:alnum:]_]*|_Unwind_[[:alnum:]_]*)($|[[:space:]])' "$elf.symbols"; then
      echo "FAIL: allocator/atomic/exception runtime in $elf"; exit 1
    fi
    "$dump" -d "$elf" >"$elf.asm"
    for insn in 'mrs.*PRIMASK' 'msr.*PRIMASK' 'cpsid' 'sev' 'wfe'; do
      if ! grep -Ei "$insn" "$elf.asm" >/dev/null; then
        echo "FAIL: missing $insn in $elf"; exit 1
      fi
    done
    echo "PASS: $cpu -O$opt kernel/port freestanding link, symbols, instructions"
  done
done
