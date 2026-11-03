#!/usr/bin/env python3
"""Procedural Fire Training: layout/physics, pawn movement, Metal captures and network snapshots."""
import argparse,json,os,re,subprocess,time
from pathlib import Path
from client_shutdown import settle_clients
p=argparse.ArgumentParser(description=__doc__)
p.add_argument('--game',action='store_true',help='Run the real pawn ladder probe without rendering')
p.add_argument('--team-size',type=int,choices=range(1,17),help='Expected team size for game/network probes')
p.add_argument('--render',action='store_true',help='Launch L_FireTraining and capture plan/base/catwalk/steel detail/distant views')
p.add_argument('--seed',type=int,default=42,help='Initial game or network seed')
p.add_argument('--next-seed',type=int,default=73,help='Network regeneration seed')
p.add_argument('--network',action='store_true',help='Verify initial/late LAN geometry and regeneration')
p.add_argument('--seeds',default='42,73,7919',help='Gallery seeds for --render')
p.add_argument('--offscreen',action='store_true',help='Use real rendering without showing a game window')
args=p.parse_args();args.render|=args.offscreen
if args.network and args.render:p.error('--network runs separately from the Metal gallery')
root=Path(__file__).resolve().parents[1];out=root/'Saved/Testing';out.mkdir(parents=True,exist_ok=True)
exe=Path(os.environ.get('UE_ROOT','/Users/Shared/Epic Games/UE_5.8'))/'Engine/Binaries/Mac/UnrealEditor.app/Contents/MacOS/UnrealEditor'
common=[str(exe),str(root/'RespawnAndRage.uproject'),'-nosound','-unattended','-NoSplash','-Multiprocess','-stdout','-FullStdOutLogOutput']
if args.team_size:common+=['-ini:Game:[RespawnAndRage.ServerRules]:Map.ExpectedTeamSize='+str(args.team_size)]
if args.network:
    common+=['-Multiprocess','-game','-nullrhi','-ExecCmds=t.MaxFPS 60','-MULTIHOME=0.0.0.0']
    processes=[];handles=[];paths={}
    def launch(role,url,extra=()):
        paths[role]=out/('fire-training-network-'+role+'.log')
        handle=paths[role].open('w');handles.append(handle)
        process=subprocess.Popen(common+[url]+list(extra),stdout=handle,stderr=subprocess.STDOUT)
        processes.append(process);return process
    def wait_for(marker):
        deadline=time.monotonic()+150
        while time.monotonic()<deadline:
            if marker in paths['host'].read_text(errors='replace'):return
            if processes[0].poll() is not None:raise RuntimeError('Host exited before '+marker)
            time.sleep(.25)
        raise RuntimeError('Timed out: '+marker)
    try:
        host=launch('host',f'/Game/EclipseRecoil/Maps/L_FireTraining?listen?Name=Host?MapSeed={args.seed}',
                    ['-ERFireTrainingProbe','-ERFireTrainingNetwork','-ERFireTrainingNextSeed='+str(args.next_seed),'-port=17953'])
        wait_for('ER_FIRE_NETWORK_STATE phase=1 ');launch('initial','127.0.0.1:17953?Name=Initial')
        wait_for('ER_FIRE_NETWORK_STATE phase=2 ');launch('late','127.0.0.1:17953?Name=Late')
        host.wait(timeout=240)
        host_text=paths['host'].read_text(errors='replace')
        states={int(a):int(b) for a,b in re.findall(r'ER_FIRE_NETWORK_STATE phase=(\d+) digest=(\d+)',host_text)}
        passed=host.returncode==0 and 'ER_FIRE_NETWORK_COMPLETE result=PASS' in host_text
        for role,phases in [('initial',[1,2,3]),('late',[2,3])]:
            client_text=paths[role].read_text(errors='replace')
            for phase in phases:
                good=f'ER_FIRE_CLIENT_PASS phase={phase} digest={states.get(phase)} ' in client_text
                print(role,'phase',phase,'PASS' if good else 'FAIL');passed &= good
            passed &= 'ER_FIRE_CLIENT_FAIL' not in client_text and 'ER_BLOCK_NET_INCOMPATIBLE' not in client_text
        (out/'fire-training-network-report.json').write_text(json.dumps(dict(result='PASS' if passed else 'FAIL',states=states,logs={k:str(v) for k,v in paths.items()}),indent=2)+'\n')
        raise SystemExit(0 if passed else 1)
    finally:
        settle_clients(processes,handles)
        for process in processes:
            if process.poll() is None:process.terminate()
        for process in processes:
            try:process.wait(timeout=10)
            except subprocess.TimeoutExpired:process.kill();process.wait()
        for handle in handles:handle.close()
if not(args.render or args.game):
    path=out/'fire-training-automation.log'
    with path.open('w') as f:
        r=subprocess.run(common+['-nullrhi','-ExecCmds=Automation RunTests EclipseRecoil.Destruction.FireTraining','-TestExit=Automation Test Queue Empty'],stdout=f,stderr=subprocess.STDOUT,timeout=900)
    text=path.read_text(errors='replace')
    passed=r.returncode==0 and all('Result={Success} Name={'+s+'}' in text for s in ['FireTrainingLayout','FireTrainingPhysics']) and 'Result={Fail}' not in text
    print('\n'.join(x for x in text.splitlines() if 'ER_FIRE_TRAINING_' in x or 'Result={' in x));print('Log:',path)
    raise SystemExit(0 if passed else 1)
path=out/'fire-training-metal.log'
command=common+[f'/Game/EclipseRecoil/Maps/L_FireTraining?Name=Host?MapSeed={args.seed}','-game','-windowed','-ResX=1600','-ResY=900','-ERFireTrainingProbe','-ERFireTrainingSeeds='+args.seeds,
                '-ExecCmds=t.MaxFPS 60,sg.GlobalIlluminationQuality 0,sg.ReflectionQuality 0,sg.ShadowQuality 1']+(['-RenderOffscreen'] if args.offscreen else [])
if args.game and not args.render:command.append('-nullrhi')
started_at=time.time()
with path.open('w') as f:r=subprocess.run(command,stdout=f,stderr=subprocess.STDOUT,timeout=600)
text=path.read_text(errors='replace')
passed=r.returncode==0 and 'ER_FIRE_PROBE_COMPLETE result=PASS' in text
for line in text.splitlines():
    if 'ER_FIRE_' in line or 'ER_RECON_RESET' in line or 'ER_RECON_BASE' in line:print(line)
shots=sorted(str(x) for x in out.glob('FireTraining-*.png') if x.stat().st_mtime>=started_at)
(out/'fire-training-report.json').write_text(json.dumps(dict(result='PASS' if passed else 'FAIL',captures=shots,
    galleries=re.findall(r'ER_FIRE_GALLERY [^\n\[]*',text),log=str(path)),indent=2)+'\n')
print('Captures:',*shots,sep='\n  ')
raise SystemExit(0 if passed else 1)
