#!/usr/bin/env python3
"""Opt-in local integration test using two actual Unreal game processes.

No cook, packaging or distribution. Outputs are stored in Saved/Testing.
The replicated probe only exists when -ERMovementProbe is explicitly supplied.
"""
from pathlib import Path
import os
import subprocess
import sys
import time
import argparse
import tempfile
from client_shutdown import settle_clients

parser = argparse.ArgumentParser(description=__doc__)
modes = parser.add_mutually_exclusive_group()
modes.add_argument("--base-only", action="store_true", help="Run run/jump/vault/climb/crouch checks only")
modes.add_argument("--posture-only", action="store_true", help="Run sprint/posture phases only")
parser.add_argument("--render", action="store_true", help="Capture the stamina HUD with Metal on the host")
parser.add_argument("--remapped-controls", action="store_true", help="Run posture phases with an isolated remapped client profile")
args = parser.parse_args()
if args.remapped_controls:
    if args.base_only:
        parser.error("--remapped-controls requires the posture phases")
    args.posture_only = True

project = Path(__file__).resolve().parents[1]
engine = Path(os.environ.get("UE_ROOT", "/Users/Shared/Epic Games/UE_5.8"))
editor = engine / "Engine/Binaries/Mac/UnrealEditor.app/Contents/MacOS/UnrealEditor"
logs = project / "Saved/Testing"
logs.mkdir(parents=True, exist_ok=True)
common = [str(editor), str(project / "RespawnAndRage.uproject"), "-game", "-nullrhi", "-nosound", "-unattended", "-NoSplash", "-Multiprocess", "-stdout", "-FullStdOutLogOutput", "-ExecCmds=t.MaxFPS 60"]
if args.posture_only: common.append("-ERPostureProbe")
if args.base_only: common.append("-ERMovementBaseProbe")
processes = []
handles = []
profile = tempfile.TemporaryDirectory() if args.remapped_controls else None
try:
    capture_started = time.time()
    host_log = (logs / "movement-host.log").open("w")
    handles.append(host_log)
    host_render = ["-windowed", "-ResX=1280", "-ResY=720", "-ERStaminaGallery"] if args.render else []
    host_common = [arg for arg in common if arg != "-nullrhi"] if args.render else common
    host = subprocess.Popen(host_common + host_render + ["/Game/EclipseRecoil/Maps/L_Depot?listen?Name=Host", "-port=17781", "-ERMovementProbe"], stdout=host_log, stderr=subprocess.STDOUT)
    processes.append(host)
    deadline = time.monotonic() + 45
    while time.monotonic() < deadline:
        output = (logs / "movement-host.log").read_text(errors="replace")
        if "listening on port 17781" in output:
            break
        if host.poll() is not None:
            raise RuntimeError("Host exited before listening; inspect movement-host.log")
        time.sleep(.2)
    else:
        raise RuntimeError("Host did not listen within 45 seconds")
    client_log = (logs / "movement-client.log").open("w")
    handles.append(client_log)
    client_options = []
    if profile:
        ini = Path(profile.name) / "GameUserSettings.ini"
        ini.write_text("[/Script/Engine.GameUserSettings]\nVersion=5\n"
                       "[EclipseRecoil.Controls]\nW=J\nC=K\nLeftControl=I\nE=Y\n")
        client_options = [f"-GameUserSettingsINI={ini}", "-ERRemappedControls"]
    client = subprocess.Popen(common + client_options + ["127.0.0.1:17781?Name=Client"], stdout=client_log, stderr=subprocess.STDOUT)
    processes.append(client)
    deadline = time.monotonic()+220
    while time.monotonic()<deadline and host.poll() is None:
        time.sleep(.2)
    if host.poll() is None:
        raise RuntimeError("Movement probe timed out")
    output = (logs / "movement-host.log").read_text(errors="replace")
    markers = [line for line in output.splitlines() if "ER_MOVEMENT_" in line or "ER_MOVEMENT_COMPLETE" in line]
    print("\n".join(markers))
    client_output = (logs / "movement-client.log").read_text(errors="replace")
    print("\n".join(line for line in client_output.splitlines() if "ER_MOVEMENT_CLIENT_" in line))
    success = (args.base_only or all(marker in client_output for marker in (
        "ER_MOVEMENT_CLIENT_PASS owner_and_simulated_proxy_prone",
        "ER_MOVEMENT_CLIENT_PASS owner_sprint_has_extra_spread",
        "ER_MOVEMENT_CLIENT_PASS owner_stamina_drains",
        "ER_MOVEMENT_CLIENT_PASS owner_stamina_refills",
        "ER_MOVEMENT_CLIENT_PASS sprint_E_phase_48",
        "ER_MOVEMENT_CLIENT_PASS sprint_E_phase_50",
        "ER_MOVEMENT_CLIENT_PASS sprint_E_phase_52"))) and "ER_MOVEMENT_CLIENT_FAIL" not in client_output and "ER_MOVEMENT_COMPLETE PASS" in output and "ER_MOVEMENT_FAIL" not in output
    if args.render and not args.base_only:
        captures = all((logs / "Stamina" / f"Phase{phase}.png").exists() and
                       (logs / "Stamina" / f"Phase{phase}.png").stat().st_mtime >= capture_started
                       for phase in range(44, 48))
        print("ER_MOVEMENT_STAMINA_CAPTURES", "PASS" if captures else "FAIL")
        success = success and captures
    if args.remapped_controls:
        success &= "ER_MOVEMENT_CLIENT_PASS remapped_controls_loaded" in client_output
    print("Logs:", logs)
    sys.exit(0 if success else 1)
finally:
    settle_clients(processes, handles)
    for process in processes:
        if process.poll() is None:
            process.terminate()
    for process in processes:
        try:
            process.wait(timeout=10)
        except subprocess.TimeoutExpired:
            process.kill()
            process.wait()
    for handle in handles:
        handle.close()
    if profile:
        profile.cleanup()
