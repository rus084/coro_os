"""Run with the Windows probe venv (pyelftools); writes reproducible size reports."""
import json
import hashlib
import re
from pathlib import Path
from elftools.elf.elffile import ELFFile

root = Path(__file__).resolve().parents[2]
build = root / "build-nucleo-wb55"
reports = []
input_section = re.compile(r'^\s*[0-9a-f]+\s+[0-9a-f]+\s+([0-9a-f]+)\s+\d+\s+(.+\.o):\(([^)]+)\)\s*$')
for variant in ("baremetal", "freertos", "coroutines"):
    folder = build / variant
    with (folder / "firmware.elf").open("rb") as stream:
        elf = ELFFile(stream)
        sections = {s.name: int(s["sh_size"]) for s in elf.iter_sections()
                    if s["sh_flags"] & 2}
        table = elf.get_section_by_name(".symtab")
        objects = {s.name: dict(bytes=int(s["st_size"]), address=hex(s["st_value"]))
                   for s in table.iter_symbols() if s["st_info"]["type"] == "STT_OBJECT"
                   and 0x20000000 <= s["st_value"] < 0x20010000 and s["st_size"]}
    contributions = {}
    for line in (folder / "firmware.map").read_text().splitlines():
        match = input_section.match(line)
        if match and match[3].startswith((".text", ".rodata", ".isr_vector", ".ARM.extab", ".data", ".init_array")):
            name = Path(match[2]).name
            contributions[name] = contributions.get(name, 0) + int(match[1], 16)
    flash = (folder / "firmware.bin").stat().st_size
    ram = sections.get(".data", 0) + sections.get(".bss", 0)
    task_stacks = sum(x["bytes"] for name, x in objects.items()
                      if "task_stacks" in name or "idle_stack" in name)
    assert task_stacks == (4608 if variant == "freertos" else 0)
    main_stack = []
    for path in sorted(folder.glob("*.su")):
        main_stack.extend(line for line in path.read_text().splitlines() if re.search(r':main\s', line))
    hardware = json.loads((folder / "hardware-result.json").read_text()) if (folder / "hardware-result.json").exists() else None
    if hardware is not None:
        if hardware.get("status") != "passed" or hardware.get("flash_verified") is not True:
            raise ValueError(f"{variant}: hardware evidence failed/incomplete/unverified")
        current_hash = hashlib.sha256((folder / "firmware.bin").read_bytes()).hexdigest()
        if hardware.get("image_sha256") != current_hash or hardware.get("variant") != variant:
            raise ValueError(f"{variant}: stale hardware evidence for a different image")
    reports.append(dict(variant=variant, flash=flash, sections=sections, ram_objects=ram,
                        task_stack_reserves_in_bss=task_stacks, msp_reserve=sections[".msp_stack"],
                        total_reserved_ram=ram + sections[".msp_stack"], main_stack_usage=main_stack,
                        linked_contributions=contributions,
                        exidx_and_alignment=flash - sum(contributions.values()),
                        ram_objects_detail=objects, hardware=hardware))
lines = ["# STM32WB55 comparison", "", "Clang23.1.3, Cortex-M4, softfp, -Os, no LTO, section GC.",
         "Same startup/clock/GPIO/debug counters; 1kHz SysTick, HSI16.", "",
         "| Variant | Flash bytes | .data+.bss | Task stacks included in .bss | MSP reserve | Total reserved RAM |",
         "|---|---:|---:|---:|---:|---:|"]
for r in reports:
    lines.append(f'| {r["variant"]} | {r["flash"]} | {r["ram_objects"]} | {r["task_stack_reserves_in_bss"]} | {r["msp_reserve"]} | {r["total_reserved_ram"]} |')
for r in reports:
    lines += ["", f'## {r["variant"]}', "", "Linked Flash contributions:"]
    lines += [f'- {name}: {size}' for name, size in r["linked_contributions"].items()]
    lines += [f'- exidx/alignment: {r["exidx_and_alignment"]}', "", "RAM objects:"]
    lines += [f'- {name}: {obj["bytes"]}' for name, obj in r["ram_objects_detail"].items()]
    lines += ["", "main .su (included in MSP reserve, do not add twice):"] + r["main_stack_usage"]
lines += ["", "MSP reserve is a chosen budget, not measured worst-case call/IRQ stack.",
          "FreeRTOS application stacks are 4*256 words + idle128, not a minimum-RAM claim.",
          "Hardware snapshots briefly halt/resume CPU: counters prove progress, not precise timing.",
          "Images include vector table, exidx/alignment, instrumentation and memory helpers.",
          "No BLE/CPU2 stack, HAL, allocator, C library startup or per-task stack in coro_os."]
(build / "size-report.json").write_text(json.dumps(reports, indent=2), encoding="utf-8")
(build / "size-report.md").write_text("\n".join(lines) + "\n", encoding="utf-8")
print("\n".join(lines[:11]))
