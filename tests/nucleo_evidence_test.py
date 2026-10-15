"""Regression checks for failed/stale hardware evidence; run with probe venv."""
import json
import hashlib
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

root = Path(__file__).resolve().parents[1]
source = root / "examples/nucleo_wb55"
errors = []
with tempfile.TemporaryDirectory() as directory:
    sandbox = Path(directory)
    report = sandbox / "examples/nucleo_wb55/report.py"
    report.parent.mkdir(parents=True)
    shutil.copyfile(source / "report.py", report)
    for variant in ("baremetal", "freertos", "coroutines"):
        dest = sandbox / "build-nucleo-wb55" / variant
        dest.mkdir(parents=True)
        for file in (root / "build-nucleo-wb55" / variant).iterdir():
            if file.name in ("firmware.elf", "firmware.bin", "firmware.map", "hardware-result.json") or file.suffix == ".su":
                shutil.copyfile(file, dest / file.name)
        evidence = dest / "hardware-result.json"
        record = json.loads(evidence.read_text())
        record.update(status="passed", flash_verified=True,
                      image_sha256=hashlib.sha256((dest / "firmware.bin").read_bytes()).hexdigest())
        evidence.write_text(json.dumps(record))
    evidence = sandbox / "build-nucleo-wb55/coroutines/hardware-result.json"
    good = json.loads(evidence.read_text())
    for label, record, expected_success in (
        ("valid", good, True),
        ("failed", dict(good, status="failed"), False),
        ("stale", dict(good, image_sha256="0" * 64), False),
        ("unverified", dict(good, flash_verified=False), False),
    ):
        evidence.write_text(json.dumps(record))
        completed = subprocess.run([sys.executable, str(report)], capture_output=True, text=True)
        if (completed.returncode == 0) != expected_success:
            errors.append(f"{label} evidence: expected success={expected_success}, got {completed.returncode}")
if "--hardware-stale-check" in sys.argv:
    # Change only local BIN, never target Flash. Observe-only must reject it.
    binary = root / "build-nucleo-wb55/coroutines/firmware.bin"
    original = binary.read_bytes()
    changed = bytearray(original)
    changed[-1] ^= 1
    try:
        binary.write_bytes(changed)
        completed = subprocess.run([sys.executable, str(source / "hardware.py"),
            "coroutines", "--observe-only", "--seconds", "0.1"], capture_output=True, text=True)
        if completed.returncode == 0:
            errors.append("observe-only accepted different local BIN than board Flash")
        elif "Flash readback mismatch" not in completed.stderr:
            errors.append("observe-only failed for an unrelated reason: " + completed.stderr[-800:])
    finally:
        binary.write_bytes(original)
if errors:
    raise AssertionError("\n".join(errors))
print("PASS: failed/stale/unverified evidence rejected; valid evidence accepted")
