"""Activate the approved idle/fire pose without modifying the source reload.

Run through Unreal Python. Saves only owned pistol derivatives and DA_Pistol;
the source reload and all other packages are protected by hashes. Re-runs always
start from the editing baseline and original motion, never corrected clips.
"""
from pathlib import Path
import hashlib,json,math,re,runpy,zipfile
import unreal as ue

root=Path(ue.Paths.project_dir()).resolve()
assets=ue.EditorAssetLibrary
folder='/Game/EclipseRecoil/Weapons/Sketchfab/Pistol'
editing=folder+'/KnifeGripEditing'
backup=root/'Saved/PistolManualGripBackup';backup.mkdir(parents=True,exist_ok=True)
h=runpy.run_path(str(root/'scripts/retarget_weapon_hands.py'))
xyz,quat,qmul,inv=h['xyz'],h['quat'],h['qmul'],h['inverse']
transform=h['transform']
def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def file(a):return root/('Content/'+a.get_path_name().split('.')[0].removeprefix('/Game/')+'.uasset')
def save(a):
    if not assets.save_loaded_asset(a,False):raise RuntimeError('Cannot save '+a.get_path_name())
def load_approved():
    a=ue.load_asset('/Game/EclipseRecoil/Characters/FirstPerson/A_Pistol_ApprovedPose')
    if a:return a
    a=ue.load_asset(editing+'/A_Pistol_ApprovedPose')
    if a:return a
    paths=sorted((root/'Saved/Autosaves').rglob('A_Pistol_ApprovedPose*.uasset'),key=lambda p:p.stat().st_mtime)
    if not paths:raise RuntimeError('No saved approved pose')
    package=ue.load_package(str(paths[-1]))
    a=ue.find_object(package,'A_Pistol_ApprovedPose')
    if not a:raise RuntimeError('Cannot recover approved pose')
    return a

data=ue.load_asset(folder+'/DA_Pistol')
baseline=ue.load_asset(editing+'/A_Pistol_ManualBaseline')
approved=load_approved()
if approved and approved.get_path_name().startswith('/Temp/'):
    approved=ue.AssetToolsHelpers.get_asset_tools().duplicate_asset('A_Pistol_ApprovedPose',editing,approved)
    if not approved:raise RuntimeError('Cannot preserve recovered approved pose')
    save(approved)
if not all((data,baseline,approved)):raise RuntimeError('Missing pistol pose assets')
options=ue.AnimPoseEvaluationOptions();options.should_retarget=False
options.evaluation_type=ue.AnimDataEvalType.RAW
options.optional_skeletal_mesh=ue.load_asset('/Game/EclipseRecoil/Characters/Teams/Alpha/FirstPerson/SK_ER_Belica_Alpha_BelicaArmor_Forearms')
def evaluate(a,t=0):return ue.AnimPoseExtensions.get_anim_pose_at_time(a,t,options)
def bone(p,n,space=ue.AnimPoseSpaces.LOCAL):return ue.AnimPoseExtensions.get_bone_pose(p,n,space)
def different(a,b):
    return (a.translation-b.translation).length()>.001 or abs(sum(x*y for x,y in zip(quat(a.rotation),quat(b.rotation))))<.9999999 or (a.scale3d-b.scale3d).length()>.0001
bp,ap=evaluate(baseline),evaluate(approved)
names=[str(n) for n in ue.AnimPoseExtensions.get_bone_names(ap)]
changed=[n for n in names if different(bone(bp,n),bone(ap,n))]
if not changed or any(not n.endswith('_l') for n in changed):
    raise RuntimeError('Expected approved support-arm-only correction: '+str(changed))

forearm=runpy.run_path(str(root/'scripts/setup_pistol_support_forearm.py'))
forearm_shift,forearm_report=forearm['solve'](ap,data)
final_ap=forearm['adjust'](ap,forearm_shift)

originals={k:ue.load_asset(folder+'/A_Pistol_Hands_'+label) for k,label in [('idle_animation','Idle'),('fire_animation','Fire'),('reload_animation','Reload')]}
outputs={k:folder+'/A_Pistol_Hands_Manual'+label for k,label in [('idle_animation','Ready'),('fire_animation','Fire')]}
for mode in ('Idle', 'Fire'):
    if assets.get_metadata_tag(data, 'ERPistolSource' + mode) == '1':
        outputs.pop(mode.lower() + '_animation')
allowed={file(data).resolve()}|{(root/('Content/'+p.removeprefix('/Game/')+'.uasset')).resolve() for p in outputs.values()}
# Protect all existing private runtime assets outside the three owned packages.
protected={p.resolve():sha(p) for sub in ('characters','weapons','maps') for p in (root/'Assets'/sub/'Content').rglob('*.uasset') if p.resolve() not in allowed}
closer_archive=backup/'before-idle-fire.zip'
if not closer_archive.exists():
    with zipfile.ZipFile(closer_archive,'x',zipfile.ZIP_DEFLATED) as z:
        for a in (data,*(ue.load_asset(p) for p in outputs.values())):
            if a:z.write(file(a),str(file(a).relative_to(root)))
    with zipfile.ZipFile(closer_archive) as z:
        if z.testzip():raise RuntimeError('Forearm backup corrupt')
archive=backup/'before.zip'
if not archive.exists():
    with zipfile.ZipFile(archive,'x',zipfile.ZIP_DEFLATED) as z:
        for a in (data,baseline,*originals.values()):z.write(file(a),str(file(a).relative_to(root)))
    with zipfile.ZipFile(archive) as z:
        if z.testzip():raise RuntimeError('Backup corrupt')
        for a in (data,baseline,*originals.values()):
            if hashlib.sha256(z.read(str(file(a).relative_to(root)))).hexdigest()!=sha(file(a)):raise RuntimeError('Backup mismatch')

# Only animation references may change.
protected_fields=('mesh','weapon_idle_animation','weapon_fire_animation','weapon_reload_animation','first_person_grip_transform','first_person_palm_grip','view_offset','grip_transform','damage','hitscan_range','shot_interval','payload','magazine_size','reload_duration')
fields={p:re.sub(r'0x[0-9a-fA-F]+', 'ADDRESS', str(data.get_editor_property(p))) for p in protected_fields}

def write(a,poses):
    c=a.get_editor_property('controller');c.open_bracket('Approved manual pistol support pose',False)
    try:
        c.set_frame_rate(ue.FrameRate(numerator=30,denominator=1),False)
        c.set_number_of_frames(ue.FrameNumber(value=len(poses)-1),False)
        for n in names:
            keys=[bone(p,n) for p in poses]
            if not c.set_bone_track_keys(n,[t.translation for t in keys],[t.rotation for t in keys],[t.scale3d for t in keys],False):raise RuntimeError('Cannot write '+n)
    finally:c.close_bracket(False)
    save(a)

def blend_rotation(a,b,w):
    qa,qb=quat(a),quat(b)
    if sum(x*y for x,y in zip(qa,qb))<0:qb=tuple(-x for x in qb)
    q=tuple(x*(1-w)+y*w for x,y in zip(qa,qb));size=math.sqrt(sum(x*x for x in q))
    return tuple(x/size for x in q)

result={};checks=[]
for prop,path in outputs.items():
    source=originals[prop]
    output=ue.load_asset(path) or assets.duplicate_asset(source.get_path_name(),path)
    if not output:raise RuntimeError('Cannot create '+path)
    if prop=='idle_animation':poses=[final_ap,final_ap]
    else:
        count=max(1,round(source.get_play_length()*30))
        poses=[evaluate(source,min(i/30,source.get_play_length())) for i in range(count+1)]
        for i,p in enumerate(poses):
            weight=1.
            for n in changed:
                current=bone(p,n);base=bone(bp,n);target=bone(ap,n)
                dq=qmul(quat(target.rotation),inv(quat(base.rotation)))
                corrected=qmul(dq,quat(current.rotation))
                q=blend_rotation(current.rotation,ue.Quat(x=corrected[0],y=corrected[1],z=corrected[2],w=corrected[3]),weight)
                pos=h['add'](xyz(current.translation),h['mul'](h['sub'](xyz(target.translation),xyz(base.translation)),weight))
                scale=tuple(x*(1+weight*(y/z-1)) for x,y,z in zip(xyz(current.scale3d),xyz(target.scale3d),xyz(base.scale3d)))
                p=ue.AnimPoseExtensions.set_bone_pose(p,transform(pos,q,scale),n,ue.AnimPoseSpaces.LOCAL)
            poses[i]=forearm['adjust'](p,forearm_shift,weight)
    write(output,poses)
    options.evaluation_type=ue.AnimDataEvalType.COMPRESSED
    # Check every authored idle bone  after compression.
    for t in ([0] if prop=='idle_animation' else []):
        actual=evaluate(output,t)
        for n in (names if prop=='idle_animation' else changed):
            a,b=bone(actual,n),bone(final_ap,n)
            if (a.translation-b.translation).length()>.02 or abs(sum(x*y for x,y in zip(quat(a.rotation),quat(b.rotation))))<.9999:
                raise RuntimeError('Compressed approved pose mismatch '+prop+' '+n)
    options.evaluation_type=ue.AnimDataEvalType.RAW
    result[prop]=output

for p,a in result.items():data.set_editor_property(p,a)
if fields!={p:re.sub(r'0x[0-9a-fA-F]+', 'ADDRESS', str(data.get_editor_property(p))) for p in protected_fields}:raise RuntimeError('Protected data fields changed')
assets.set_metadata_tag(data,'ERPistolManualGripApproved','1');save(data)
for p,digest in protected.items():
    if sha(p)!=digest:raise RuntimeError('Protected package changed '+str(p))
receipt=dict(forearm=forearm_report,approved=approved.get_path_name(),changed_bones=changed,clips={p:a.get_path_name() for p,a in result.items()},protected_packages=len(protected),backup=str(archive))
(backup/'receipt.json').write_text(json.dumps(receipt,indent=2)+'\n')
ue.log('ER_MANUAL_PISTOL_ACTIVATED '+json.dumps(receipt))
