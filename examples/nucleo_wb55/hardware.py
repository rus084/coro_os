"""Windows pyOCD flash/readback/run and brief halted RAM snapshots."""
import argparse
import hashlib
import json
import time
from pathlib import Path

from elftools.elf.elffile import ELFFile
from pyocd.core.helpers import ConnectHelper
from pyocd.flash.loader import MemoryLoader

ROOT = Path(__file__).resolve().parents[2]
parser = argparse.ArgumentParser()
parser.add_argument("variant", choices=("baremetal", "freertos", "coroutines"))
parser.add_argument("--observe-only", action="store_true")
parser.add_argument("--seconds", type=float, default=2)
parser.add_argument("--exercise-button", action="store_true",
                    help="Drive PC4 open-drain through SWD to exercise GPIO/EXTI/debounce")
parser.add_argument("--probe", help="ST-LINK UID; omit when a single probe is attached")
args = parser.parse_args()
output = ROOT / "build-nucleo-wb55" / args.variant
pack = ROOT / "build-hardware-probe/Keil.STM32WBxx_DFP.2.0.1.pack"
with (output / "firmware.elf").open("rb") as stream:
    elf = ELFFile(stream)
    symbols = elf.get_section_by_name(".symtab")
    info = symbols.get_symbol_by_name("debug_state")[0]
    debug_address, debug_words = info["st_value"], info["st_size"] // 4
    tick_symbol = "xTickCount" if args.variant == "freertos" else "board_ticks"
    tick_address = symbols.get_symbol_by_name(tick_symbol)[0]["st_value"]
image = (output / "firmware.bin").read_bytes()
assert 8 <= len(image) <= 65536
image_hash = hashlib.sha256(image).hexdigest()
evidence_path = output / "hardware-result.json"
evidence_path.write_text(json.dumps(dict(status="incomplete", variant=args.variant,
    image_sha256=image_hash, flash_verified=False), indent=2), encoding="utf-8")
session = ConnectHelper.session_with_chosen_probe(
    unique_id=args.probe, target_override="stm32wb55rgvx", pack=[str(pack)],
    frequency=1000000, connect_mode="attach" if args.observe_only else "under-reset",
    auto_unlock=False, resume_on_disconnect=True, hide_programming_progress=True,
    options={"cache.enable_memory": False})
if session is None:
    raise RuntimeError("ST-LINK unavailable")
with session:
    target = session.target
    assert target.read32(0xE0042000) & 0xFFF == 0x495
    if not args.observe_only:
        target.reset_and_halt()
        # Flash algorithm runs in RAM; application IRQs must not interrupt it.
        target.write32(0xE000E010, 0) # SysTick CTRL
        for offset in range(0, 12, 4):
            target.write32(0xE000E180 + offset, 0xFFFFFFFF) # NVIC ICER
            target.write32(0xE000E280 + offset, 0xFFFFFFFF) # NVIC ICPR
        target.write_core_register("primask", 1)
        target.flush()
        assert target.read32(0x58004020) & 0xFF == 0xAA, "RDP is not level0"
        secure_start = 0x08000000 + (target.read32(0x58004080) & 0xFF) * 4096
        assert 0x08000000 + ((len(image) + 4095) // 4096) * 4096 <= secure_start
        loader = MemoryLoader(session, chip_erase="sector", smart_flash=False,
                              keep_unwritten=False)
        loader.add_data(0x08000000, image)
        loader.commit()
        target.halt()
    # Also bind observe-only results to actual board bytes, not merely local BIN.
    target.halt()
    actual = bytes(target.read_memory_block8(0x08000000, len(image)))
    assert actual == image, "Flash readback mismatch"
    print(f"Verified {len(image)} Flash bytes", flush=True)
    if not args.observe_only:
        target.reset_and_halt()
    target.resume()
    def snapshot():
        state = str(target.get_state())
        target.halt() # STM32WB SRAM reads can return zero while CPU is asleep.
        words = target.read_memory_block32(debug_address, debug_words)
        names = ("signature", "variant", "irq_count", "press_count", "fast")
        result = dict(zip(names, words[:5]), tick=target.read32(tick_address),
                      toggles=words[5:8], fault=words[8], core_state=state,
                      pc=hex(target.read_core_register("pc")),
                      cfsr=target.read32(0xE000ED28), hfsr=target.read32(0xE000ED2C))
        target.resume()
        return result
    time.sleep(0.15)
    first = snapshot()
    time.sleep(args.seconds)
    second = snapshot()
    result = dict(variant=args.variant, debug_address=hex(debug_address),
                  status="passed", flash_verified=True, image_sha256=image_hash,
                  first=first, second=second, seconds=args.seconds)
    if args.exercise_button:
        # Board SW1 shorts PC4 to ground. Open-drain only sinks current; never
        # drive high against the physical switch. Restore GPIO input afterwards.
        gpio = 0x48000800
        bit = 1 << 4
        target.halt()
        saved_mode, saved_type = target.read32(gpio), target.read32(gpio + 4)
        saved_output = target.read32(gpio + 0x14) & bit
        assert target.read32(gpio + 0x10) & bit, "Release physical SW1 before exercise"
        try:
            target.write32(gpio + 0x18, bit)
            target.write32(gpio + 4, saved_type | bit)
            target.write32(gpio, (saved_mode & ~(3 << 8)) | (1 << 8))
            target.resume()
            time.sleep(0.08)
            before = snapshot()
            target.write32(gpio + 0x18, bit << 16)
            time.sleep(0.006)
            target.write32(gpio + 0x18, bit)
            time.sleep(0.08)
            short = snapshot()
            assert short["press_count"] == before["press_count"], "Short press accepted"
            assert short["fast"] == before["fast"]
            target.write32(gpio + 0x18, bit << 16)
            time.sleep(0.12)
            pressed = snapshot()
            time.sleep(0.3)
            held = snapshot()
            assert pressed["press_count"] == before["press_count"] + 1
            assert pressed["fast"] == (before["fast"] ^ 1)
            assert held["press_count"] == pressed["press_count"], "Hold repeated"
            # Brief release bounce while held must not rearm a second press.
            target.write32(gpio + 0x18, bit)
            time.sleep(0.006)
            target.write32(gpio + 0x18, bit << 16)
            time.sleep(0.12)
            bounced = snapshot()
            assert bounced["press_count"] == pressed["press_count"], "Release bounce rearmed"
            target.write32(gpio + 0x18, bit)
            time.sleep(0.08)
            target.write32(gpio + 0x18, bit << 16)
            time.sleep(0.12)
            repeated = snapshot()
            assert repeated["press_count"] == before["press_count"] + 2
            assert repeated["fast"] == before["fast"]
            assert repeated["irq_count"] >= before["irq_count"] + 2
            assert repeated["fault"] == 0
            result["button_exercise"] = dict(method="PC4 open-drain driven via SWD",
                before=before, short=short, pressed=pressed, held=held,
                bounced=bounced, repeated=repeated)
        finally:
            target.halt()
            target.write32(gpio + 0x18, bit) # Release before restoring input.
            target.write32(gpio, saved_mode)
            target.write32(gpio + 4, saved_type)
            target.write32(gpio + 0x18, bit if saved_output else bit << 16)
            target.resume()
        time.sleep(0.08)
    assert second["signature"] == 0x57423535 and second["fault"] == 0
    assert second["variant"] == {"baremetal": 1, "freertos": 2, "coroutines": 3}[args.variant]
    assert second["cfsr"] == 0 and second["hfsr"] == 0
    assert second["core_state"].endswith("RUNNING") or second["core_state"].endswith("SLEEPING")
    assert (second["tick"] - first["tick"]) & 0xFFFFFFFF > 0, "No tick progress"
    assert all(b > a for a, b in zip(first["toggles"], second["toggles"])), "LED task stalled"
    evidence_path.write_text(json.dumps(result, indent=2), encoding="utf-8")
    print(json.dumps(result, indent=2), flush=True)
