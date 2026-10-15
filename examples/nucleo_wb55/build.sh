#!/usr/bin/env bash
set -euo pipefail
root="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/../.." && pwd)"
src="$root/examples/nucleo_wb55"
build="$root/build-nucleo-wb55"
deps="$build/deps"
cxx="${NOALLOC_CXX:?Set NOALLOC_CXX to the Clang compiler with coro::noalloc support}"
lld="${NOALLOC_LLD:-ld.lld}"
test -f "$deps/device/stm32wb55xx.h" || { echo 'Run prepare.ps1 first'; exit 1; }
flags=(--target=arm-none-eabi -mcpu=cortex-m4 -mthumb -mfpu=fpv4-sp-d16 -mfloat-abi=softfp
  -Os -g -ffreestanding -ffunction-sections -fdata-sections -fstack-usage
  -fno-unwind-tables -fno-asynchronous-unwind-tables -nostdinc++
  -isystem /usr/lib/arm-none-eabi/include/c++/14.2.1
  -isystem /usr/lib/arm-none-eabi/include/c++/14.2.1/arm-none-eabi/thumb/v7e-m/nofp
  -isystem /usr/lib/arm-none-eabi/include -I"$deps/cmsis" -I"$deps/device"
  -I"$root/include" -I"$src" -DSTM32WB55xx -Wall -Wextra -Werror)
variants=("${1:-all}")
if [[ ${variants[0]} == all ]]; then variants=(baremetal freertos coroutines); fi
for variant in "${variants[@]}"; do
  case "$variant" in baremetal|freertos|coroutines) ;; *) exit 2 ;; esac
  out="$build/$variant"
  mkdir -p "$out"
  objects=()
  for file in startup board memory "$variant"; do
    extra=()
    if [[ $variant == freertos ]]; then
      extra=(-I"$deps/freertos/include" -I"$deps/freertos/portable/GCC/ARM_CM4F")
    fi
    "$cxx" "${flags[@]}" -std=c++20 -fno-exceptions -fno-rtti -fno-c++-static-destructors \
      "${extra[@]}" -c "$src/$file.cpp" -o "$out/$file.o"
    objects+=("$out/$file.o")
  done
  if [[ $variant == freertos ]]; then
    for file in tasks list queue portable/GCC/ARM_CM4F/port; do
      obj="$out/$(basename "$file").o"
      "$cxx" "${flags[@]}" -x c -std=c11 -Wno-unused-command-line-argument -Wno-unused-but-set-global \
        -I"$deps/freertos/include" -I"$deps/freertos/portable/GCC/ARM_CM4F" \
        -c "$deps/freertos/$file.c" -o "$obj"
      objects+=("$obj")
    done
  fi
  "$cxx" --target=arm-none-eabi -mcpu=cortex-m4 -mthumb -mfpu=fpv4-sp-d16 -mfloat-abi=softfp \
    -nostdlib "--ld-path=$lld" "${objects[@]}" \
    -Wl,--gc-sections,-T,"$src/stm32wb55.ld",-Map,"$out/firmware.map" -o "$out/firmware.elf"
  arm-none-eabi-objcopy -O binary "$out/firmware.elf" "$out/firmware.bin"
  arm-none-eabi-nm -S -n -C "$out/firmware.elf" > "$out/symbols.txt"
  test -z "$(arm-none-eabi-nm -u "$out/firmware.elf")"
  if grep -E 'operator new|operator delete| malloc$| free$|pvPortMalloc|vPortFree' "$out/symbols.txt"; then exit 1; fi
  arm-none-eabi-size -A "$out/firmware.elf" > "$out/sections.txt"
  echo "$variant: $(wc -c < "$out/firmware.bin") bytes Flash"
done
