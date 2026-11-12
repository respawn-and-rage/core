"""Keep the first-person arms continuous between idle, fire and reload.

The fire and reload hand clips were solved with a fixed elbow pole that differs
from the fitted idle pose, so the left elbow jumped 12-16 cm outward on the
first frame of every shot and reload. This re-solves the left arm of those
clips bending toward the idle elbow. Where a clip starts or ends with a hand
away from its idle pose (pistol support hand, the end of the Carbine reload),
that residual is faded out over the first/last frames. Fingers, the weapon
clips and every weapon/gameplay field stay as they are.

Run with UnrealEditor -run=pythonscript. Idempotent; saves only the owned
A_<Key>_Hands_Fire / A_<Key>_Hands_Reload clips after backing them up.
"""
from pathlib import Path
import hashlib,json,math,shutil
import unreal as ue

root=Path(ue.Paths.project_dir()).resolve();assets=ue.EditorAssetLibrary
backup=root/'Saved/FPSSupportElbowBackup/Revision1';backup.mkdir(parents=True,exist_ok=True)
keys=['Pistol','SMG','Shotgun','Assault','Carbine','Sniper','Launcher']
arms={'l':['upperarm_l','lowerarm_l','hand_l'],'r':['upperarm_r','lowerarm_r','hand_r']}

def add(a,b):return tuple(x+y for x,y in zip(a,b))
def sub(a,b):return tuple(x-y for x,y in zip(a,b))
def mul(a,s):return tuple(x*s for x in a)
def dot(a,b):return sum(x*y for x,y in zip(a,b))
def cross(a,b):return (a[1]*b[2]-a[2]*b[1],a[2]*b[0]-a[0]*b[2],a[0]*b[1]-a[1]*b[0])
def length(a):return math.sqrt(dot(a,a))
def unit(a):return mul(a,1/max(1e-8,length(a)))
def xyz(v):return (v.x,v.y,v.z)
def quat(q):return (q.x,q.y,q.z,q.w)
def qmul(a,b):return (*add(add(mul(b[:3],a[3]),mul(a[:3],b[3])),cross(a[:3],b[:3])),a[3]*b[3]-dot(a[:3],b[:3]))
def inverse(q):return (-q[0],-q[1],-q[2],q[3])
def shortest(q):return q if q[3]>=0 else mul(q,-1)
def from_to(a,b):
    a,b=unit(a),unit(b)
    return unit((*cross(a,b),1+dot(a,b)))
def transform(position,rotation,scale):
    result=ue.Transform()
    result.set_editor_property('translation',ue.Vector(*position))
    result.set_editor_property('rotation',ue.Quat(x=rotation[0],y=rotation[1],z=rotation[2],w=rotation[3]))
    result.set_editor_property('scale3d',ue.Vector(*scale))
    return result
def file_of(asset):
    return root/('Assets/weapons/Content/'+asset.get_path_name().split('.')[0].removeprefix('/Game/')+'.uasset')
def sha(path):return hashlib.sha256(path.read_bytes()).hexdigest()

options=ue.AnimPoseEvaluationOptions();options.set_editor_property('should_retarget',False)
options.set_editor_property('optional_skeletal_mesh',ue.load_asset('/Game/EclipseRecoil/Characters/Teams/Alpha/FirstPerson/SK_ER_Twinblast_Alpha_Officer_Forearms'))
def world(pose,name):return ue.AnimPoseExtensions.get_bone_pose(pose,name,ue.AnimPoseSpaces.WORLD)

receipt=[]
for key in keys:
    path=f'/Game/EclipseRecoil/Weapons/Sketchfab/{key}/DA_{key}'
    data=assets.load_asset(path)
    if not data:raise RuntimeError('Missing '+path)
    data_hash=sha(file_of(data))
    idle=data.get_editor_property('idle_animation')
    idle_pose=ue.AnimPoseExtensions.get_anim_pose_at_time(idle,0,options)
    rest={side:[world(idle_pose,name) for name in names] for side,names in arms.items()}
    for prop in ('fire_animation','reload_animation'):
        clip=data.get_editor_property(prop)
        if not clip:continue
        expected=f'A_{key}_Hands_'+('Fire' if prop=='fire_animation' else 'Reload')
        if clip.get_name()!=expected:raise RuntimeError('Unexpected clip '+clip.get_path_name())
        if key=='Pistol' and prop=='reload_animation' and assets.get_metadata_tag(clip,'ERPistolReloadArmRevision')=='1':
            continue
        file=file_of(clip);old=backup/file.name
        if not old.exists():
            shutil.copy2(file,old)
            if old.read_bytes()!=file.read_bytes():raise RuntimeError('Backup mismatch')
        frames=max(1,round(clip.get_play_length()*30))
        poses=[ue.AnimPoseExtensions.get_anim_pose_at_time(clip,min(index/30,clip.get_play_length()),options) for index in range(frames+1)]
        tracks={};report={}
        for side,names in arms.items():
            rest_shoulder,rest_elbow,rest_wrist=[xyz(b.translation) for b in rest[side]]
            rest_hand=quat(rest[side][2].rotation)
            # A clip that starts or ends away from the idle hand pops when it
            # hands over. Fade that residual out over the first/last frames; a
            # short fire clip carries its (single) residual throughout.
            ends=[world(poses[i],names[2]) for i in (0,frames)]
            residual=[(sub(rest_wrist,xyz(e.translation)),shortest(qmul(rest_hand,inverse(quat(e.rotation))))) for e in ends]
            if prop=='fire_animation':residual[1]=residual[0]
            fade=1e9 if prop=='fire_animation' else min(10,frames//2)
            moved=any(length(r[0])>.05 or abs(r[1][3])<.99996 for r in residual)
            if side=='r' and not moved:continue
            tracks.update({name:[] for name in names});largest=0
            for index,pose in enumerate(poses):
                bones=[world(pose,name) for name in names]
                shoulder,elbow,wrist=[xyz(b.translation) for b in bones]
                hand=quat(bones[2].rotation)
                for weight,(shift,turn) in zip((max(0,1-index/fade),max(0,1-(frames-index)/fade)) if prop!='fire_animation' else (1,0),residual):
                    if weight<=0:continue
                    wrist=add(wrist,mul(shift,weight))
                    hand=qmul(unit(add(mul(turn,weight),(0,0,0,1-weight))),hand)
                upper=length(sub(elbow,shoulder));lower=length(sub(xyz(bones[2].translation),elbow))
                distance=length(sub(wrist,shoulder));direction=unit(sub(wrist,shoulder))
                reach=max(abs(upper-lower)+.01,min(upper+lower-.01,distance))
                wrist=add(shoulder,mul(direction,reach))
                # Support arm bends toward its idle elbow; the weapon arm keeps its solved elbow.
                toward=sub(rest_elbow if side=='l' else elbow,shoulder)
                bend=sub(toward,mul(direction,dot(toward,direction)))
                if length(bend)<1e-4:bend=sub(sub(elbow,shoulder),mul(direction,dot(sub(elbow,shoulder),direction)))
                bend=unit(bend)
                along=(upper*upper-lower*lower+reach*reach)/(2*reach)
                target=add(add(shoulder,mul(direction,along)),mul(bend,math.sqrt(max(0,upper*upper-along*along))))
                largest=max(largest,length(sub(target,elbow)))
                rotations=[qmul(from_to(sub(rest_elbow,rest_shoulder),sub(target,shoulder)),quat(rest[side][0].rotation)),
                           qmul(from_to(sub(rest_wrist,rest_elbow),sub(wrist,target)),quat(rest[side][1].rotation)),hand]
                for name,position,rotation,bone in zip(names,[shoulder,target,wrist],rotations,bones):
                    pose=ue.AnimPoseExtensions.set_bone_pose(pose,transform(position,rotation,xyz(bone.scale3d)),name,ue.AnimPoseSpaces.WORLD)
                if length(sub(xyz(world(pose,names[2]).translation),wrist))>.05:raise RuntimeError('Wrist target missed '+clip.get_name())
                for name in names:tracks[name].append(ue.AnimPoseExtensions.get_bone_pose(pose,name,ue.AnimPoseSpaces.LOCAL))
            report[side]=dict(start_residual_cm=round(length(residual[0][0]),3),end_residual_cm=round(length(residual[1][0]),3),largest_elbow_change_cm=round(largest,3))
        controller=clip.get_editor_property('controller')
        controller.open_bracket('Continuous first-person support elbow',False)
        try:
            for name,frames_keys in tracks.items():
                if not controller.set_bone_track_keys(name,[k.translation for k in frames_keys],[k.rotation for k in frames_keys],[k.scale3d for k in frames_keys],False):
                    raise RuntimeError('Support arm track failed '+name)
        finally:controller.close_bracket(False)
        assets.set_metadata_tag(clip,'ERSupportElbowRevision','1')
        if not assets.save_loaded_asset(clip,False):raise RuntimeError('Cannot save '+clip.get_path_name())
        receipt.append(dict(clip=clip.get_path_name(),frames=frames+1,arms=report,
                            before_sha256=sha(old),after_sha256=sha(file)))
        ue.log('ER_FPS_SUPPORT_ELBOW '+json.dumps(receipt[-1]))
    if sha(file_of(data))!=data_hash:raise RuntimeError('Protected weapon data changed '+path)
(backup/'receipt.json').write_text(json.dumps(receipt,indent=2)+'\n')
print('ER_FPS_SUPPORT_ELBOW_SETUP PASS clips='+str(len(receipt)))
