#!/usr/bin/env python3
"""Validate all world weapon contacts, scales, aim states and skins on two UE peers."""
from pathlib import Path
import os
import argparse
import subprocess
import time
from client_shutdown import settle_clients

parser=argparse.ArgumentParser(description=__doc__)
parser.add_argument('--prone',action='store_true',help='Check weapon and elbow floor clearance on all skins')
parser.add_argument('--wall',action='store_true',help='Add thin cover ahead of the prone weapon')
parser.add_argument('--knife',action='store_true',help='Check the knife and its authored grip on all 20 skins')
parser.add_argument('--catalog-only',action='store_true',help='Check one rifle pose on each skin')
parser.add_argument('--side-wall',action='store_true',help='Add a close side wall to check leg clearance')
args=parser.parse_args()
if (args.wall or args.side_wall) and not args.prone:parser.error('--wall and --side-wall require --prone')

project=Path(__file__).resolve().parents[1]
engine=Path(os.environ.get('UE_ROOT','/Users/Shared/Epic Games/UE_5.8'))
editor=engine/'Engine/Binaries/Mac/UnrealEditor.app/Contents/MacOS/UnrealEditor'
logs=project/'Saved/Testing';logs.mkdir(parents=True,exist_ok=True)
common=[str(editor),str(project/'RespawnAndRage.uproject'),'-game','-nullrhi','-nosound','-unattended','-NoSplash','-Multiprocess','-stdout','-FullStdOutLogOutput','-ExecCmds=t.MaxFPS 60']
tag=('world-poses-prone-wall' if args.wall else 'world-poses-prone' if args.prone else 'world-poses-network')+('-knife' if args.knife else '')+('-side' if args.side_wall else '')
hostlog=logs/(tag+'-host.log');clientlog=logs/(tag+'-client.log')
if args.prone:common+=['-ERWorldPoseProne','-ini:Engine:[SystemSettings]:r.MeshStreaming=0']
if args.wall:common.append('-ERWorldPoseWall')
if args.side_wall:common.append('-ERWorldPoseSideWall')
if args.catalog_only:common.append('-ERWorldPoseAllGallery')
if args.knife:common+=['-ERWorldPoseItem=0','-ERWorldPoseAllGallery']
expected=20 if args.knife or args.catalog_only else 260
processes=[];handles=[]
try:
    handles.append(hostlog.open('w'))
    host=subprocess.Popen(common+['/Game/EclipseRecoil/Maps/L_Depot?listen?ERFullArsenalTest?ERAlphaBots=1?EROmegaBots=0?ERBotMode=Target?Name=PoseHost','-port=17967','-ERWorldPoseProbe','-ERWorldPoseExternal','-ERWorldPoseNetwork'],stdout=handles[-1],stderr=subprocess.STDOUT)
    processes.append(host);deadline=time.monotonic()+60
    while 'listening on port 17967' not in hostlog.read_text(errors='replace'):
        if host.poll() is not None or time.monotonic()>deadline:raise RuntimeError('World-pose host did not listen')
        time.sleep(.2)
    handles.append(clientlog.open('w'))
    client=subprocess.Popen(common+['127.0.0.1:17967?Name=PoseClient'],stdout=handles[-1],stderr=subprocess.STDOUT)
    processes.append(client)
    host.wait(timeout=300)
    server=hostlog.read_text(errors='replace');remote=clientlog.read_text(errors='replace')
    server_count=server.count('ER_WORLD_POSE_PASS sample=')
    client_count=sum('ER_WORLD_POSE_PASS sample=' in line and 'client=1' in line for line in remote.splitlines())
    complete=f'ER_WORLD_POSE_COMPLETE result=PASS samples={expected}' in server
    clean='ER_WORLD_POSE_FAIL' not in server and 'ER_WORLD_POSE_FAIL' not in remote
    print(f'ER_WORLD_POSES_NETWORK server={server_count}/{expected} client={client_count}/{expected} complete={int(complete)} clean={int(clean)}')
    raise SystemExit(0 if server_count==expected and client_count==expected and complete and clean and host.returncode==0 else 1)
finally:
    settle_clients(processes,handles)
    for process in processes:
        if process.poll() is None:
            process.terminate()
            try:process.wait(timeout=5)
            except subprocess.TimeoutExpired:process.kill();process.wait()
    for handle in handles:handle.close()
