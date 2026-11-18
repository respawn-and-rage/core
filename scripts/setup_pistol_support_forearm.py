"""Bring the approved pistol elbow toward the camera, outside the 16:9 viewport.

Used by apply_manual_pistol_grip.py. Moves the elbow toward the camera, keeping both segment lengths and the
complete hand transform unchanged. The hidden shoulder follows minimally.
"""
from pathlib import Path
import math,runpy
import unreal as ue
h=runpy.run_path(str(Path(__file__).with_name('retarget_weapon_hands.py')))
add,sub,mul,dot,cross,length,unit,xyz,quat,qmul,from_to,transform=[h[n] for n in ('add','sub','mul','dot','cross','length','unit','xyz','quat','qmul','from_to','transform')]
NAMES=('upperarm_l','lowerarm_l','hand_l')

def frames(pose):
    return [ue.AnimPoseExtensions.get_bone_pose(pose,n,ue.AnimPoseSpaces.WORLD) for n in NAMES]

def adjust(pose,shift,weight=1.):
    bones=frames(pose);shoulder,elbow,wrist=[xyz(b.translation) for b in bones]
    upper=length(sub(elbow,shoulder));lower=length(sub(wrist,elbow))
    target=add(wrist,mul(unit(sub(add(elbow,mul(shift,weight)),wrist)),lower))
    # Move the hidden shoulder minimally rather than stretch either segment.
    new_shoulder=add(target,mul(unit(sub(shoulder,target)),upper))
    rotations=[qmul(from_to(sub(elbow,shoulder),sub(target,new_shoulder)),quat(bones[0].rotation)),
               qmul(from_to(sub(wrist,elbow),sub(wrist,target)),quat(bones[1].rotation)),quat(bones[2].rotation)]
    for n,pos,rot,b in zip(NAMES,(new_shoulder,target,wrist),rotations,bones):
        pose=ue.AnimPoseExtensions.set_bone_pose(pose,transform(pos,rot,xyz(b.scale3d)),n,ue.AnimPoseSpaces.WORLD)
    after=frames(pose)
    if (after[2].translation-bones[2].translation).length()>.001:raise RuntimeError('Support wrist moved')
    for i in (0,1):
        if abs((bones[i+1].translation-bones[i].translation).length()-(after[i+1].translation-after[i].translation).length())>.001:
            raise RuntimeError('Support arm length changed')
    return pose

def solve(pose,data):
    bones=frames(pose);wrist=xyz(bones[2].translation)
    lower=length(sub(wrist,xyz(bones[1].translation)))
    defaults=ue.get_default_object(ue.load_asset('/Game/EclipseRecoil/Core/BP_ERCharacter').generated_class())
    base=defaults.first_person_body.get_editor_property('relative_location')
    body=ue.Transform(location=base+data.view_offset,rotation=ue.Rotator(pitch=0,yaw=-90,roll=0))
    skin=ue.load_asset('/Game/EclipseRecoil/Characters/Teams/Alpha/FirstPerson/SK_ER_Belica_Alpha_BelicaArmor_Forearms')
    vertices=[v for v in ue.ERCharacter.get_presentation_vertices(skin,'lowerarm_l') if v.x<=15.]
    if not vertices:raise RuntimeError('Missing support forearm geometry')
    wrist_camera=ue.MathLibrary.transform_location(body,bones[2].translation)
    candidates=[]
    for i in range(4096):
        z=1-2*(i+.5)/4096;radius=math.sqrt(1-z*z);angle=i*math.pi*(3-math.sqrt(5))
        target=add(wrist,mul((radius*math.cos(angle),radius*math.sin(angle),z),lower))
        camera=ue.MathLibrary.transform_location(body,ue.Vector(*target))
        if camera.x<=0 or camera.x>wrist_camera.x or camera.z/camera.x>-.59 or camera.y>wrist_camera.y:continue
        turn=qmul(from_to(sub(wrist,xyz(bones[1].translation)),sub(wrist,target)),quat(bones[1].rotation))
        frame=transform(target,turn,xyz(bones[1].scale3d))
        safe=True
        for v in vertices:
            point=ue.MathLibrary.transform_location(body,ue.MathLibrary.transform_location(frame,v))
            if point.x>.01 and abs(point.y/point.x)<1.02 and abs(point.z/point.x)<.59:
                safe=False;break
        if safe:candidates.append((camera.length(),target,xyz(camera)))
    if not candidates:raise RuntimeError('No offscreen elbow solution')
    distance,target,camera=min(candidates)
    original=ue.MathLibrary.transform_location(body,bones[1].translation)
    return sub(target,xyz(bones[1].translation)),dict(elbow_camera_cm=camera,old_distance_cm=original.length(),distance_cm=distance,forearm_vertices_checked=len(vertices))
