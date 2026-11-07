#!/usr/bin/env python3
"""Town layout/physics automation, pawn walking, renders and LAN snapshots."""
import argparse
import os
from pathlib import Path
import re
import subprocess
import time
from client_shutdown import settle_clients

p = argparse.ArgumentParser(description=__doc__)
p.add_argument('--game', action='store_true')
p.add_argument('--render', action='store_true')
p.add_argument('--network', action='store_true')
p.add_argument('--offscreen', action='store_true')
p.add_argument('--seed', type=int, default=42)
p.add_argument('--team-size', type=int, choices=range(1, 17), default=2)
args = p.parse_args()
root = Path(__file__).resolve().parents[1]
out = root/'Saved/Testing'
out.mkdir(parents=True, exist_ok=True)
exe = Path(os.environ.get('UE_ROOT', '/Users/Shared/Epic Games/UE_5.8'))/'Engine/Binaries/Mac/UnrealEditor.app/Contents/MacOS/UnrealEditor'
common = [str(exe), str(root/'RespawnAndRage.uproject'), '-nosound', '-unattended', '-NoSplash', '-Multiprocess', '-stdout', '-FullStdOutLogOutput']
if not (args.game or args.render or args.offscreen or args.network):
    log = out/'town-automation.log'
    with log.open('w') as handle:
        result = subprocess.run(common+['/Engine/Maps/Entry', '-nullrhi', '-ExecCmds=Automation RunTests EclipseRecoil.Maps.Town', '-TestExit=Automation Test Queue Empty'], stdout=handle, stderr=subprocess.STDOUT, timeout=900)
    text = log.read_text(errors='replace')
    print('\n'.join(line for line in text.splitlines() if 'Result={' in line or ('Error:' in line and 'Condition failed' not in line) or 'ER_TOWN_LAYOUT seeds=' in line))
    print('Log:', log)
    raise SystemExit(0 if result.returncode == 0 and text.count('Result={Success}') == 2 and 'Result={Fail}' not in text else 1)

children, handles, paths = [], [], {}
def launch(role, url, render=False):
    path = out/('town-'+role+'.log')
    paths[role] = path
    handle = path.open('w')
    handles.append(handle)
    command = common+[url, '-game', '-windowed', '-ResX=1600', '-ResY=900', '-ERTownProbe', '-port=17979',
        '-ini:Game:[RespawnAndRage.ServerRules]:Map.ExpectedTeamSize='+str(args.team_size),
        '-ExecCmds=t.MaxFPS 60,sg.GlobalIlluminationQuality 0,sg.ReflectionQuality 0,sg.ShadowQuality 1']
    if not render:
        command.append('-nullrhi')
    elif args.offscreen:
        command.append('-RenderOffscreen')
    proc = subprocess.Popen(command, cwd=root, stdout=handle, stderr=subprocess.STDOUT)
    children.append(proc)
    return proc
def read(role):
    return paths[role].read_text(errors='replace')
def wait(marker):
    deadline = time.monotonic()+300
    while marker not in read('host'):
        if children[0].poll() is not None or time.monotonic() > deadline:
            raise RuntimeError('Host stopped or timed out before '+marker)
        time.sleep(.2)
try:
    started = time.time()
    url = '/Game/EclipseRecoil/Maps/L_Town?Name=Host?MapSeed='+str(args.seed)+'?ERAlphaBots=0?EROmegaBots=0'+('?listen' if args.network else '')
    host = launch('host', url, args.render or args.offscreen)
    if args.network:
        wait('ER_TEMPLATE_STATE phase=1 ')
        launch('initial', '127.0.0.1:17979?Name=Initial')
        wait('ER_TEMPLATE_STATE phase=2 ')
        launch('late', '127.0.0.1:17979?Name=Late')
    host.wait(timeout=300+args.team_size*10)
    text = read('host')
    passed = host.returncode == 0 and 'ER_TOWN_COMPLETE result=PASS' in text and 'ER_TEMPLATE_PROBE_FAIL' not in text
    passed &= 'ER_TEMPLATE_PROBE_PASS town_pawn_walked_all_primary_spawn_exits' in text
    passed &= len(re.findall(r'ER_TOWN_WALK_END kind=spawn_exit path=\d+ result=PASS', text)) == 4
    passed &= 'ER_TEMPLATE_PROBE_PASS town_regenerate_different_seed_restores_cells' in text
    states = {int(a): int(b) for a, b in re.findall(r'ER_TEMPLATE_STATE phase=(\d+) digest=(\d+)', text)}
    if args.network:
        for role, phases in [('initial', [1, 2, 3]), ('late', [2, 3])]:
            client = read(role)
            for phase in phases:
                good = f'ER_TOWN_CLIENT_PASS phase={phase} digest={states.get(phase)}' in client
                print(role, phase, 'PASS' if good else 'FAIL')
                passed &= good
            passed &= 'ER_TOWN_CLIENT_FAIL' not in client and 'ER_BLOCK_NET_INCOMPATIBLE' not in client
    if args.render or args.offscreen:
        passed &= all((out/('Town-'+name+'.png')).exists() and (out/('Town-'+name+'.png')).stat().st_mtime >= started for name in ['Aerial', 'Shop', 'Street'])
    print('\n'.join(line for line in text.splitlines() if 'ER_TOWN_' in line or 'ER_TEMPLATE_PROBE_' in line or 'ER_TEMPLATE_STATE' in line))
    print('Logs:', *paths.values())
    raise SystemExit(0 if passed else 1)
finally:
    settle_clients(children, handles)
    for proc in children:
        if proc.poll() is None:
            proc.terminate()
    for proc in children:
        try:
            proc.wait(timeout=10)
        except subprocess.TimeoutExpired:
            proc.kill()
    for handle in handles:
        handle.close()
