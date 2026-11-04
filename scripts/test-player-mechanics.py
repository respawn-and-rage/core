#!/usr/bin/env python3
"""Actual listen-server/autonomous-client player mechanics, no saved test maps."""
from pathlib import Path
import subprocess,time,os,sys,argparse
from client_shutdown import settle_clients
args=argparse.ArgumentParser();args.add_argument('--render',action='store_true');args.add_argument('--fps',type=int,default=60);args=args.parse_args()
p=Path(__file__).resolve().parents[1];logs=p/'Saved/Testing';logs.mkdir(exist_ok=True,parents=True)
exe=Path(os.environ.get('UE_ROOT','/Users/Shared/Epic Games/UE_5.8'))/'Engine/Binaries/Mac/UnrealEditor.app/Contents/MacOS/UnrealEditor'
common=[str(exe),str(p/'RespawnAndRage.uproject'),'-game','-unattended','-nosound','-NoSplash','-Multiprocess','-stdout','-FullStdOutLogOutput','-ExecCmds=t.MaxFPS '+str(args.fps),'-ERTraversalProbe']
procs=[];handles=[]
try:
    def start(name,extra):
        h=(logs/f'player-mechanics-{name}.log').open('w');handles.append(h)
        pr=subprocess.Popen(common+extra,stdout=h,stderr=subprocess.STDOUT);procs.append(pr);return pr
    host=start('host',['/Game/EclipseRecoil/Maps/L_Depot?listen?ERFullArsenalTest?Name=Host','-port=17793']+(['-windowed','-ResX=1280','-ResY=720','-ERPerformanceAudit'] if args.render else ['-nullrhi']))
    end=time.monotonic()+60
    while time.monotonic()<end:
        if 'listening on port 17793' in (logs/'player-mechanics-host.log').read_text(errors='replace'):break
        if host.poll() is not None:raise RuntimeError('host exited before listen')
        time.sleep(.2)
    else:raise RuntimeError('listen timeout')
    client=start('client',['127.0.0.1:17793?Name=Client','-nullrhi','-PktLag=60','-PktLagVariance=15'])
    end=time.monotonic()+120
    while host.poll() is None and time.monotonic()<end:time.sleep(.2)
    texts=[(logs/f'player-mechanics-{name}.log').read_text(errors='replace') for name in ['host','client']]
    for txt in texts:
        print('\n'.join(l for l in txt.splitlines() if 'ER_TRAVERSAL_' in l or 'ER_FALL_DAMAGE' in l))
    success='ER_TRAVERSAL_COMPLETE PASS' in texts[0] and texts[1].count('ER_TRAVERSAL_CLIENT_PASS')>=3 and all('ER_TRAVERSAL_FAIL' not in t and 'ER_TRAVERSAL_CLIENT_FAIL' not in t for t in texts)
    sys.exit(0 if success else 1)
finally:
    settle_clients(procs,handles)
    for pr in procs:
        if pr.poll() is None:pr.terminate()
    for pr in procs:
        try:pr.wait(timeout=10)
        except subprocess.TimeoutExpired:pr.kill();pr.wait()
    for h in handles:h.close()
