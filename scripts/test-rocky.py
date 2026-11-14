#!/usr/bin/env python3
"""Run Rocky layout and physical collision automation in the Unreal Editor."""

import argparse
import os
from pathlib import Path
import re
import subprocess
import time

from client_shutdown import settle_clients


parser = argparse.ArgumentParser(description=__doc__)
mode = parser.add_mutually_exclusive_group()
mode.add_argument('--game', action='store_true', help='Walk a full crossing with the game pawn')
mode.add_argument('--network', action='store_true', help='Also verify initial and late loopback clients')
mode.add_argument('--render', action='store_true', help='Capture the arena, tunnel, trail and summit in Metal')
parser.add_argument('--seed', type=int, default=42)
parser.add_argument('--team-size', type=int, choices=range(1, 17), default=2)
parser.add_argument('--port', type=int, default=17989)
args = parser.parse_args()
root = Path(__file__).resolve().parents[1]
editor = (Path(os.environ.get('UE_ROOT', '/Users/Shared/Epic Games/UE_5.8'))
          / 'Engine/Binaries/Mac/UnrealEditor.app/Contents/MacOS/UnrealEditor')
out = root / 'Saved/Testing'
out.mkdir(parents=True, exist_ok=True)
common = [str(editor), str(root / 'RespawnAndRage.uproject'), '-nosound',
          '-unattended', '-NoSplash', '-Multiprocess', '-stdout', '-FullStdOutLogOutput']


def run_game():
    processes, handles = [], []
    paths = {}

    def launch(role, url):
        paths[role] = out / f'rocky-{role}.log'
        handle = paths[role].open('w')
        handles.append(handle)
        command = common + [url, '-game', '-ERRockyProbe', f'-port={args.port}',
                            f'-ini:Game:[RespawnAndRage.ServerRules]:Map.ExpectedTeamSize={args.team_size}']
        if args.render:
            command += ['-ERRockyGallery','-RenderOffscreen','-windowed','-ResX=1600','-ResY=1000',
                        '-EROutdoorLightingSeed=2','-ExecCmds=t.MaxFPS 60,sg.GlobalIlluminationQuality 0,sg.ReflectionQuality 0']
        else:
            command.append('-nullrhi')
        process = subprocess.Popen(command, cwd=root, stdout=handle, stderr=subprocess.STDOUT)
        processes.append(process)
        return process

    def read(role):
        return paths[role].read_text(errors='replace')

    def wait_phase(phase):
        marker = f'ER_TEMPLATE_STATE phase={phase} '
        deadline = time.monotonic() + 110
        while marker not in read('host'):
            if host.poll() is not None or time.monotonic() >= deadline:
                raise RuntimeError(f'Host did not reach phase {phase}. See {paths["host"]}')
            time.sleep(.2)

    try:
        url = f'/Game/EclipseRecoil/Maps/L_Rocky?MapSeed={args.seed}?Name=Host?ERAlphaBots=0?EROmegaBots=0'
        started = time.time()
        host = launch('gallery' if args.render else 'host', url + ('?listen' if args.network else ''))
        if args.network:
            wait_phase(1)
            launch('initial', f'127.0.0.1:{args.port}?Name=Initial')
            wait_phase(2)
            launch('late', f'127.0.0.1:{args.port}?Name=Late')
        host.wait(timeout=180)
        if args.render:
            captures = [out / f'Rocky-{view}.png' for view in ('Overview','Tunnel','Trail','Summit')]
            return host.returncode == 0 and all(path.is_file() and path.stat().st_mtime >= started for path in captures)
        state = read('host')
        passed = host.returncode == 0 and 'ER_ROCKY_GAME result=PASS' in state
        print('\n'.join(line for line in state.splitlines()
                        if 'ER_TEMPLATE_' in line or 'ER_ROCKY_GAME' in line))
        if args.network:
            digests = dict(re.findall(r'ER_TEMPLATE_STATE phase=(\d+) digest=(\d+)', state))
            for role, phases in (('initial', ('1', '2', '3')), ('late', ('2', '3'))):
                client = read(role)
                for phase in phases:
                    ready = f'ER_ROCKY_PEER_PASS phase={phase} digest={digests.get(phase)}' in client
                    print(role, 'phase', phase, 'PASS' if ready else 'FAIL')
                    passed &= ready
                passed &= 'ER_BLOCK_NET_INCOMPATIBLE' not in client and 'ER_ROCKY_PEER_FAIL' not in client
        return passed
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
        print('Logs', *paths.values())


if args.game or args.network or args.render:
    raise SystemExit(0 if run_game() else 1)

log = out / 'rocky-automation.log'
with log.open('w') as handle:
    result = subprocess.run(
        common + ['/Engine/Maps/Entry', '-nullrhi', '-ExecCmds=Automation RunTests EclipseRecoil.Maps.Rocky',
         '-TestExit=Automation Test Queue Empty'],
        cwd=root, stdout=handle, stderr=subprocess.STDOUT, timeout=1800,
    )
lines = log.read_text(errors='replace').splitlines()
for line in lines:
    if 'LogAutomationController: Error:' in line or any(token in line for token in
            ('Result={', 'ER_ROCKY_LAYOUT', 'ER_ROCKY_PHYSICS', 'ER_MAP_BUILD_EXHAUSTED', 'ER_MAP_COLLISION_REJECT')):
        print(line)
print('Log', log)
success = {name for line in lines for name in re.findall(r'Result=\{Success\} Name=\{(\w+)\}', line)}
raise SystemExit(0 if result.returncode == 0 and success == {'Layout', 'Physics', 'RockClimb'} else 1)
