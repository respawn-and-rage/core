#!/usr/bin/env python3
"""Two local peers: slow map admission, authoritative protection, RTT/loss telemetry."""
import argparse
import json
import os
import re
from pathlib import Path
import subprocess
import time

from client_shutdown import settle_clients

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--lag', type=int, default=75, help='Outgoing one-way packet delay on each peer, ms')
parser.add_argument('--jitter', type=int, default=20, help='Packet lag variance, ms')
parser.add_argument('--loss', type=int, default=2, help='Outgoing loss percentage on each peer')
parser.add_argument('--client-fps', type=int, default=15)
parser.add_argument('--require-checkpoint', action='store_true', help='Pause live delta application after admission so recovery must commit a real payload')
args = parser.parse_args()
if not (0 <= args.lag <= 500 and 0 <= args.jitter <= 250 and 0 <= args.loss <= 10 and 5 <= args.client_fps <= 240):
    parser.error('Use lag 0..500, jitter 0..250, loss 0..10, client-fps 5..240')
root = Path(__file__).resolve().parents[1]
out = root/'Saved/Testing'
out.mkdir(parents=True, exist_ok=True)
editor = Path(os.environ.get('UE_ROOT', '/Users/Shared/Epic Games/UE_5.8'))/'Engine/Binaries/Mac/UnrealEditor.app/Contents/MacOS/UnrealEditor'
common = [str(editor), str(root/'RespawnAndRage.uproject'), '-game', '-nullrhi', '-nosound', '-unattended', '-NoSplash', '-Multiprocess', '-stdout', '-FullStdOutLogOutput',
          f'-PktLag={args.lag}', f'-PktLagVariance={args.jitter}', f'-PktLoss={args.loss}', '-ERNetworkAudit']
paths = {role: out/f'network-readiness-{role}.log' for role in ('host', 'client')}
processes, handles = [], []


def launch(role, url, *extra):
    handle = paths[role].open('w')
    handles.append(handle)
    process = subprocess.Popen(common+[url, *extra], stdout=handle, stderr=subprocess.STDOUT, cwd=root)
    processes.append(process)
    return process


try:
    host = launch('host', '/Game/EclipseRecoil/Maps/L_Town?listen?Name=ReadyHost?MapSeed=42', '-port=17993', '-ERNetworkReadinessProbe',
                  '-ini:Game:[RespawnAndRage.ServerRules]:Map.ExpectedTeamSize=2', '-ExecCmds=t.MaxFPS 60')
    deadline = time.monotonic()+90
    while 'listening on port 17993' not in paths['host'].read_text(errors='replace'):
        if host.poll() is not None or time.monotonic() >= deadline:
            raise RuntimeError('Host did not listen; inspect network-readiness-host.log')
        time.sleep(.2)
    launch('client', '127.0.0.1:17993?Name=SlowReadyClient', '-ERMapSyncDelay=8', f'-ExecCmds=t.MaxFPS {args.client_fps}',
           *(['-ERCellRecoveryProbe'] if args.require_checkpoint else []))
    host.wait(timeout=90)
    server = paths['host'].read_text(errors='replace')
    client = paths['client'].read_text(errors='replace')
    passed = host.returncode == 0 and 'ER_NET_READINESS_COMPLETE result=PASS' in server and 'ER_NET_READINESS_FAIL' not in server
    passed &= 'ER_NET_QUALITY Ping ' in client
    periods = [float(value) for value in re.findall(r'ER_NET_QUALITY .*server ([\d.]+) ms', client)]
    passed &= bool(periods) and max(periods) > 0
    passed &= 'ER_CELL_RECOVERY_APPLIED' in client and 'ER_CELL_RECOVERY_START' in server
    if args.require_checkpoint:
        passed &= any('ER_CELL_RECOVERY_APPLIED' in line and 'committed=1' in line for line in client.splitlines())
    print('\n'.join(line for line in server.splitlines() if 'ER_NET_READINESS_' in line))
    print('Telemetry:', 'PASS' if 'ER_NET_QUALITY Ping ' in client else 'FAIL')
    report = {'result': 'PASS' if passed else 'FAIL', 'conditions': vars(args), 'scope': 'two local UE Editor game processes; no physical LAN or Linux package', 'logs': {key: str(path) for key, path in paths.items()}}
    (out/'network-readiness-report.json').write_text(json.dumps(report, indent=2)+'\n')
    raise SystemExit(0 if passed else 1)
finally:
    settle_clients(processes, handles)
    for process in processes:
        if process.poll() is None:
            process.terminate()
        try:
            process.wait(timeout=10)
        except subprocess.TimeoutExpired:
            process.kill()
            process.wait()
    for handle in handles:
        handle.close()
