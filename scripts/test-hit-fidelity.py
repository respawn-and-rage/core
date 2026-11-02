#!/usr/bin/env python3
"""Compare the visible figure with server hit bodies, floor contact, and shot timing."""
from pathlib import Path
import argparse
import math
import os
import re
import statistics
import subprocess
import time
from client_shutdown import settle_clients

parser=argparse.ArgumentParser(description=__doc__)
parser.add_argument('--lag',type=int,nargs='*',help='One-way PktLag values in ms: run two peers instead of geometry')
parser.add_argument('--map',default='L_Depot')
args=parser.parse_args()
project=Path(__file__).resolve().parents[1]
engine=Path(os.environ.get('UE_ROOT','/Users/Shared/Epic Games/UE_5.8'))
editor=engine/'Engine/Binaries/Mac/UnrealEditor.app/Contents/MacOS/UnrealEditor'
logs=project/'Saved/Testing';logs.mkdir(parents=True,exist_ok=True)
base=[str(editor),str(project/'RespawnAndRage.uproject'),'-game','-nullrhi','-nosound','-unattended','-NoSplash','-Multiprocess','-stdout','-FullStdOutLogOutput','-ExecCmds=t.MaxFPS 60']
travel=f'/Game/EclipseRecoil/Maps/{args.map}?ERFullArsenalTest?ERAlphaBots=0?EROmegaBots=0'
fields=lambda line:dict(re.findall(r'(\w+)=([^ ]+)',line))

def number(row,key):return float(row[key])
def spread(values):return f'{min(values):.2f}..{max(values):.2f} mean {statistics.fmean(values):.2f}'

def geometry():
    path=logs/f'hit-fidelity-{args.map}.log'
    with path.open('w') as out:
        code=subprocess.run(base+[travel+'?Name=Fidelity','-ERHitFidelityProbe'],stdout=out,stderr=subprocess.STDOUT,timeout=420).returncode
    text=path.read_text(errors='replace').splitlines()
    pose=[fields(l) for l in text if 'ER_HIT_FIDELITY_POSE' in l]
    view=[fields(l) for l in text if 'ER_HIT_FIDELITY_VIEW' in l]
    complete=any('ER_HIT_FIDELITY_COMPLETE' in l for l in text) and any('ER_HIT_HISTORY_QUERY result=PASS' in l for l in text)
    print(f'ER_HIT_FIDELITY_RUN exit={code} complete={int(complete)} poses={len(pose)} views={len(view)}')
    for name,keep in [('stand knife',lambda r:r['posture']=='stand' and r['slot']=='0' and r['moving']=='0'),
                      ('stand rifle',lambda r:r['posture']=='stand' and r['slot']!='0' and r['moving']=='0'),
                      ('crouch rifle',lambda r:r['posture']=='crouch' and r['slot']!='0'),
                      ('prone',lambda r:r['posture']=='prone'),
                      ('running rifle',lambda r:r['moving']=='1')]:
        rows=[r for r in pose if keep(r)];cells=[r for r in view if keep(r)]
        if not rows or not cells:continue
        print(f'{name}: body_cm {spread([number(r,"body_max_cm") for r in rows])}  body_deg {spread([number(r,"body_max_deg") for r in rows])}')
        print(f'  capsule_gap {spread([number(r,"capsule_gap") for r in rows])}  sole_gap {spread([number(r,"sole_gap") for r in rows])}')
        print(f'  missed% {spread([number(r,"missed_pct") for r in cells])}  phantom% {spread([number(r,"phantom_pct") for r in cells])}')
        print(f'  margin3 missed {spread([number(r,"margin3_missed") for r in cells])} phantom {spread([number(r,"margin3_phantom") for r in cells])}  margin5 missed {spread([number(r,"margin5_missed") for r in cells])} phantom {spread([number(r,"margin5_phantom") for r in cells])}')
        print(f'  deep_miss% {spread([number(r,"deep_miss_pct") for r in cells])}  deep_phantom% {spread([number(r,"deep_phantom_pct") for r in cells])}  feet_missed% {spread([number(r,"feet_missed_pct") for r in cells])}')
    # Bodies must follow the rendered bones in every state; soles touch flat floors when still.
    bodies=bool(pose) and all(number(r,'body_max_cm')<1 for r in pose)
    soles=bool(pose) and all(abs(number(r,'sole_gap'))<1 for r in pose if r['posture']!='prone' and r['moving']=='0')
    print(f'ER_HIT_FIDELITY_RESULT bodies={int(bodies)} soles={int(soles)}')
    raise SystemExit(0 if code==0 and complete and bodies and soles else 1)

def lag(ms):
    hostlog=logs/f'hit-lag-{ms}-host.log';clientlog=logs/f'hit-lag-{ms}-client.log'
    processes=[];handles=[]
    try:
        handles.append(hostlog.open('w'))
        host=subprocess.Popen(base+[travel+'?listen?Name=Target','-port=17971','-ERHitLagProbe',f'-PktLag={ms}'],stdout=handles[-1],stderr=subprocess.STDOUT)
        processes.append(host);deadline=time.monotonic()+90
        while 'listening on port 17971' not in hostlog.read_text(errors='replace'):
            if host.poll() is not None or time.monotonic()>deadline:raise RuntimeError('Lag host did not listen')
            time.sleep(.2)
        handles.append(clientlog.open('w'))
        client=subprocess.Popen(base+['127.0.0.1:17971?Name=Shooter','-ERHitLagProbe',f'-PktLag={ms}'],stdout=handles[-1],stderr=subprocess.STDOUT)
        processes.append(client)
        host.wait(timeout=120)
    finally:
        settle_clients(processes,handles)
        for process in processes:
            if process.poll() is None:
                process.terminate()
                try:process.wait(timeout=5)
                except subprocess.TimeoutExpired:process.kill();process.wait()
        for handle in handles:handle.close()
    def read(path,tag):
        rows={}
        for line in path.read_text(errors='replace').splitlines():
            match=re.search(tag+r' shot=(\d+) target=V\(X=([-\d.]+), Y=([-\d.]+), Z=[-\d.]+\) yaw=([-\d.]+)',line)
            if match:rows[int(match[1])]=dict(target=(float(match[2]),float(match[3])),yaw=float(match[4]))
        return rows
    server=read(hostlog,'ER_HIT_LAG_SERVER');rewound=read(hostlog,'ER_HIT_LAG_REWOUND');shots=read(clientlog,'ER_HIT_LAG_CLIENT')
    gaps=[];yaws=[]
    for shot,row in shots.items():
        if shot not in server:continue
        gaps.append(math.dist(row['target'],server[shot]['target']))
        yaws.append(abs((row['yaw']-server[shot]['yaw']+180)%360-180))
    compensated=[math.dist(row['target'],rewound[shot]['target']) for shot,row in shots.items() if shot in rewound]
    print(f'compensated target_offset_cm {spread(compensated) if compensated else "none"}')
    if not gaps or len(compensated)!=len(gaps):raise RuntimeError('Missing shot samples')
    if ms>0 and statistics.fmean(compensated)>=statistics.fmean(gaps):raise RuntimeError('Rewind did not reduce mean target error')
    exact=sum(error<.5 for error in yaws)
    print(f'lag one-way={ms}ms shots={len(gaps)}: target_offset_cm {spread(gaps) if gaps else "none"}  aim_exact {exact}/{len(yaws)}  aim_yaw_error_deg {spread(yaws) if yaws else "none"} at 120 deg/s')

if args.lag is None:geometry()
else:
    for ms in args.lag:lag(ms)
