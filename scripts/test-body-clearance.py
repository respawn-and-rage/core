#!/usr/bin/env python3
"""Native collision laboratory: prone entry, translation, turning and weapon bounds."""
from pathlib import Path
import os
import subprocess

project = Path(__file__).resolve().parents[1]
engine = Path(os.environ.get("UE_ROOT", "/Users/Shared/Epic Games/UE_5.8"))
editor = engine / "Engine/Binaries/Mac/UnrealEditor.app/Contents/MacOS/UnrealEditor"
log = project / "Saved/Testing/body-clearance.log"
log.parent.mkdir(parents=True, exist_ok=True)
with log.open("w") as output:
    result = subprocess.run([
        str(editor), str(project / "RespawnAndRage.uproject"),
        "-nullrhi", "-nosound", "-unattended", "-NoSplash", "-stdout", "-FullStdOutLogOutput",
        "-ExecCmds=Automation RunTests EclipseRecoil.Movement.BodyClearance",
        "-TestExit=Automation Test Queue Empty",
    ], stdout=output, stderr=subprocess.STDOUT, timeout=150)
text = log.read_text(errors="replace")
events = False
for line in text.splitlines():
    events = events or "Test Completed" in line
    if events and ("Test Completed" in line or "Error:" in line):
        print(line)
success = result.returncode == 0 and "Result={Success}" in text and "BodyClearance" in text
print("ER_BODY_CLEARANCE", "PASS" if success else "FAIL", "log=", log)
raise SystemExit(0 if success else 1)
