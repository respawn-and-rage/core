#!/usr/bin/env python3
"""Real listen/client grenade fuses, tumble, smoke walls/windows and cleanup."""
from pathlib import Path
import argparse,json,os,subprocess,time,re
from client_shutdown import settle_clients
parser=argparse.ArgumentParser(description=__doc__)
parser.add_argument('--render',action='store_true',help='Render the fixture on Metal and save captures')
parser.add_argument('--combat',action='store_true',help='Test 2/6m blast, armor, cover, cook and real press/release inputs')
parser.add_argument('--audio',action='store_true',help='Enable host audio and verify explosion playback (requires --render)')
args=parser.parse_args()
if args.audio and not args.render:parser.error('--audio requires --render')
project=Path(__file__).resolve().parents[1]
engine=Path(os.environ.get('UE_ROOT','/Users/Shared/Epic Games/UE_5.8'))
editor=engine/'Engine/Binaries/Mac/UnrealEditor.app/Contents/MacOS/UnrealEditor'
logs=project/'Saved/Testing';logs.mkdir(parents=True,exist_ok=True)
tag=('grenade-combat' if args.combat else 'grenades')+('-render' if args.render else '')+('-audio' if args.audio else '')
port='17804' if args.combat else '17803'
common=[str(editor),str(project/'RespawnAndRage.uproject'),'-game','-unattended','-NoSplash','-Multiprocess','-stdout','-FullStdOutLogOutput','-ExecCmds=t.MaxFPS 60,r.DynamicRes.OperationMode 0,r.ScreenPercentage 100']
if not args.audio:common.append('-nosound')
if args.combat:common.append('-ERGrenadeCombatProbe')
processes=[];handles=[]
try:
    started=time.time();hostlog=logs/(tag+'-host.log');clientlog=logs/(tag+'-client.log')
    render=['-windowed','-ResX=1280','-ResY=720','-ERGrenadeRender'] if args.render else ['-nullrhi']
    handles.append(hostlog.open('w'))
    host=subprocess.Popen(common+render+['/Game/EclipseRecoil/Maps/L_Depot?listen?ERFullArsenalTest=1?Name=GrenadeHost','-port='+port,'-ERGrenadeProbe'],stdout=handles[-1],stderr=subprocess.STDOUT)
    processes.append(host);deadline=time.monotonic()+90
    while 'listening on port '+port not in hostlog.read_text(errors='replace'):
        if host.poll() is not None or time.monotonic()>deadline:raise RuntimeError('Grenade host failed to listen')
        time.sleep(.2)
    handles.append(clientlog.open('w'))
    client=subprocess.Popen(common+['-nosound','-nullrhi','127.0.0.1:'+port+'?Name=GrenadeClient'],stdout=handles[-1],stderr=subprocess.STDOUT)
    processes.append(client)
    host.wait(timeout=160 if args.render else 90);time.sleep(.3)
    server=hostlog.read_text(errors='replace');remote=clientlog.read_text(errors='replace')
    print('\n'.join(line for line in server.splitlines() if 'ER_GRENADE_TEST_' in line))
    checks={
        'gameplay_probe':host.returncode==0 and 'ER_GRENADE_TEST_COMPLETE result=PASS' in server and 'ER_GRENADE_TEST_FAIL' not in server,
        'remote_tumble':'ER_GRENADE_REMOTE_TUMBLE' in remote,
        'remote_hand_interpolation':'ER_GRENADE_REMOTE_INTERPOLATED kind=hand' in remote,
        'remote_launcher_interpolation':'ER_GRENADE_REMOTE_INTERPOLATED kind=launcher' in remote,
        'remote_team_colors':'ER_SMOKE_REMOTE_TEAM alpha=1 omega=0' in remote and 'ER_SMOKE_REMOTE_TEAM alpha=0 omega=1' in remote,
        'remote_both_fuses':remote.count('ER_GRENADE_BURST system=P_ER_CompactHE')>=2,
        'remote_hemisphere':'ER_SMOKE_CLOUD shape=hemisphere' in remote,
        'remote_window':'through=1 solid=0' in remote,
        'material_compile':not any('M_ER_ContainedSmoke' in line and ('Failed to compile' in line or 'Error' in line) for line in server.splitlines()),
    }
    if args.combat:
        checks={
            'gameplay_probe':checks['gameplay_probe'],
            'remote_hand_hidden':'ER_COOK_REMOTE_HELD hidden=1 bone=1' in remote,
            'remote_release':'ER_COOK_REMOTE_RELEASE' in remote,
            'material_compile':checks['material_compile'],
        }
    if args.audio:
        checks['explosion_audio_started']='ER_GRENADE_AUDIO sound=S_ER_GrenadeExplosion started=1 spatial=1 concurrency=1' in server
        checks['smoke_audio_started']='ER_SMOKE_AUDIO sound=S_ER_SmokeRelease started=1 spatial=1 concurrency=1' in server
        checks['remote_smoke_audio_asset']='ER_SMOKE_AUDIO sound=S_ER_SmokeRelease' in remote
        checks['remote_audio_asset']='ER_GRENADE_AUDIO sound=S_ER_GrenadeExplosion' in remote
        smoke_gains=[float(v) for v in re.findall(r'ER_SMOKE_AUDIO[^\n]*gain=([0-9.]+)',server+'\n'+remote)]
        checks['smoke_only_minus_10db']=bool(smoke_gains) and all(abs(v-.65*10**(-10/20))<1e-6 for v in smoke_gains)
    if args.render:
        checks['fresh_captures']=all((logs/'Grenades'/name).exists() and (logs/'Grenades'/name).stat().st_mtime>=started for name in (['Cook00_Ready.png','Cook01_Held.png','Cook02_Released.png','Cook03_RemoteReady.png'] if args.combat else ['01_Bounce.png','02_HE.png','02_HE_After.png','03_ClosedWall.png','04_OpenWindow.png','05_InsideSmoke.png','06_HE_Dispersed.png']))
    for label,value in checks.items():print('ER_GRENADE_CHECK',label,value)
    (logs/(tag+'-results.json')).write_text(json.dumps(checks,indent=2)+'\n')
    raise SystemExit(0 if all(checks.values()) else 1)
finally:
    settle_clients(processes,handles)
    for process in processes:
        if process.poll() is None:
            process.terminate()
            try:process.wait(timeout=5)
            except subprocess.TimeoutExpired:process.kill();process.wait()
    for handle in handles:handle.close()
