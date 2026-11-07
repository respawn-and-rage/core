#!/usr/bin/env python3
"""Industrial archetype: automation or real stairs/rifle/Metal/LAN late join."""
import argparse,json,os,re,subprocess,time
from pathlib import Path
from client_shutdown import settle_clients
p=argparse.ArgumentParser(description=__doc__)
p.add_argument('--game',action='store_true');p.add_argument('--render',action='store_true')
p.add_argument('--finish-gallery',action='store_true',help='Quick intact Factory material captures; no movement or destruction probe');p.add_argument('--network',action='store_true');p.add_argument('--gallery',action='store_true')
p.add_argument('--rule',type=int,choices=[0,1,2,3],default=3);p.add_argument('--seed',type=int,default=42)
p.add_argument('--executable',type=Path,help='Test a packaged Development game instead of UnrealEditor')
p.add_argument('--offscreen',action='store_true',help='Use real rendering without showing a game window')
args=p.parse_args();args.render|=args.gallery or args.finish_gallery or args.offscreen
if args.finish_gallery and args.network:p.error("Finish gallery is standalone")
if args.executable and not(args.game or args.render or args.network):p.error('--executable requires --game, --render or --network')
root=Path(__file__).resolve().parents[1];out=root/'Saved/Testing';out.mkdir(parents=True,exist_ok=True)
exe=Path(os.environ.get('UE_ROOT','/Users/Shared/Epic Games/UE_5.8'))/'Engine/Binaries/Mac/UnrealEditor.app/Contents/MacOS/UnrealEditor'
common=([str(args.executable.resolve())] if args.executable else [str(exe),str(root/'RespawnAndRage.uproject')])+['-nosound','-unattended','-NoSplash','-Multiprocess','-stdout','-FullStdOutLogOutput']
label='industrial'+('-network' if args.network else '-game' if args.game or args.render else '-automation')+('-packaged' if args.executable else '')+('-metal' if args.render else '')
if not(args.game or args.render or args.network):
    path=out/(label+'.log')
    with path.open('w') as f:
        r=subprocess.run(common+['-nullrhi','-ExecCmds=Automation RunTests EclipseRecoil.Destruction.CentralBuilding','-TestExit=Automation Test Queue Empty'],stdout=f,stderr=subprocess.STDOUT,timeout=240)
    text=path.read_text(errors='replace')
    passed=r.returncode==0 and all('Result={Success} Name={'+s+'}' in text for s in ['CentralBuildingLayout','CentralBuildingCollision','CentralBuildingEvolution']) and 'Result={Fail}' not in text
    print('\n'.join(x for x in text.splitlines() if 'ER_CENTRAL_' in x or 'Error:' in x or 'Result={' in x));print('Log:',path)
    raise SystemExit(0 if passed else 1)
children=[];handles=[];paths={}
def launch(role,url,render=False,extra=()):
    path=out/(label+'-'+role+'.log');paths[role]=path;f=path.open('w');handles.append(f)
    command=common+[url,'-game','-windowed','-ResX=1280','-ResY=720','-ExecCmds=t.MaxFPS 60,sg.GlobalIlluminationQuality 0,sg.ReflectionQuality 0,sg.ShadowQuality 1']+list(extra)
    if not render:command.append('-nullrhi')
    if render and args.offscreen:command.append('-RenderOffscreen')
    if args.executable:command.append('-UserDir='+str(out/(label+'-user-'+role)))
    proc=subprocess.Popen(command,stdout=f,stderr=subprocess.STDOUT);children.append(proc);return proc
def read(role):return paths[role].read_text(errors='replace')
def wait_for(marker,timeout=300):
    end=time.monotonic()+timeout
    while time.monotonic()<end:
        if marker in read('host'):return
        if children[0].poll() is not None:raise RuntimeError('Host stopped before '+marker)
        time.sleep(.25)
    raise RuntimeError('Timed out: '+marker)
try:
    url=f'/Game/EclipseRecoil/Maps/L_CentralBuilding?Name=Host?MapSeed={args.seed}?MapCrossing={args.rule}?ERAlphaBots=1?EROmegaBots=1?ERBotMode=Target'+('?listen' if args.network else '')
    extra=['-ERIndustrialMapProbe','-port=17791']+(['-ERIndustrialGallery'] if args.gallery else [])+(['-ERFactoryFinishGallery'] if args.finish_gallery else [])
    host=launch('host',url,args.render,extra)
    if args.network:
        wait_for('ER_INDUSTRIAL_STATE phase=1 ');launch('initial','127.0.0.1:17791?Name=Initial')
        wait_for('ER_INDUSTRIAL_STATE phase=3 ');launch('late','127.0.0.1:17791?Name=Late')
    host.wait(timeout=420)
    text=read('host');passed=host.returncode==0 and 'ER_INDUSTRIAL_PROBE_COMPLETE result=PASS' in text and 'ER_INDUSTRIAL_PROBE_FAIL' not in text
    if not args.finish_gallery:
        passed&='ER_INDUSTRIAL_PROBE_PASS all_recorded_crossings_walked' in text
    factory_shader_ok=not any('Art/Industrial/' in line and 'Failed to compile' in line for line in text.splitlines())
    passed&=factory_shader_ok
    if not factory_shader_ok:print('FAIL: Factory material shader compilation')
    states={int(a):int(b) for a,b in re.findall(r'ER_INDUSTRIAL_STATE phase=(\d+) digest=(\d+)',text)}
    if args.network:
        for role,phases in [('initial',[1,3,4]),('late',[3,4])]:
            client=read(role)
            for phase in phases:
                good=f'ER_INDUSTRIAL_CLIENT_PASS phase={phase} digest={states.get(phase)} ' in client
                print(role,'phase',phase,'PASS' if good else 'FAIL');passed&=good
            passed&='ER_INDUSTRIAL_CLIENT_FAIL' not in client and 'ER_BLOCK_NET_INCOMPATIBLE' not in client
    for line in text.splitlines():
        if 'ER_INDUSTRIAL_' in line:print(line)
    (out/(label+'-report.json')).write_text(json.dumps(dict(result='PASS' if passed else 'FAIL',seed=args.seed,rule=args.rule,gallery=args.gallery,finish_gallery=args.finish_gallery,render=args.render,network=args.network,factory_shaders=factory_shader_ok,states=states,logs={k:str(v) for k,v in paths.items()}),indent=2)+'\n')
    raise SystemExit(0 if passed else 1)
except (RuntimeError,subprocess.TimeoutExpired) as e:
    print('FAIL',e);print('\n'.join(read('host').splitlines()[-35:]));raise SystemExit(1)
finally:
    settle_clients(children,handles)
    for proc in children:
        if proc.poll() is None:proc.terminate()
    for proc in children:
        try:proc.wait(timeout=10)
        except subprocess.TimeoutExpired:proc.kill();proc.wait()
    for f in handles:f.close()
