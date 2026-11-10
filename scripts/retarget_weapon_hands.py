"""Fit supplied wrist/root motion to the existing independent FPS skin rig.

Invoked by import_sketchfab_weapons.py for project-owned animation derivatives.
Keeps the skin geometry, segment lengths, fingers, sockets and component frame.
Source reach outside the fitted rig is bounded by two-bone IK, without moving
the gameplay capsule or camera. Original source rigs/animations stay private.
"""
import math
import unreal as ue

def add(a,b):return tuple(x+y for x,y in zip(a,b))
def sub(a,b):return tuple(x-y for x,y in zip(a,b))
def mul(a,s):return tuple(x*s for x in a)
def dot(a,b):return sum(x*y for x,y in zip(a,b))
def cross(a,b):return (a[1]*b[2]-a[2]*b[1],a[2]*b[0]-a[0]*b[2],a[0]*b[1]-a[1]*b[0])
def length(a):return math.sqrt(dot(a,a))
def unit(a):return mul(a,1/max(1e-8,length(a)))
def xyz(v):return (v.x,v.y,v.z)
def quat(q):return (q.x,q.y,q.z,q.w)
def inverse(q):return (-q[0],-q[1],-q[2],q[3])
def qmul(a,b):return (*add(add(mul(b[:3],a[3]),mul(a[:3],b[3])),cross(a[:3],b[:3])),a[3]*b[3]-dot(a[:3],b[:3]))
def rotate(q,v):return qmul(qmul(q,(*v,0)),inverse(q))[:3]
def from_to(a,b):
    a,b=unit(a),unit(b)
    return unit((*cross(a,b),1+dot(a,b)))
def transform(position,rotation,scale):
    result=ue.Transform()
    result.set_editor_property('translation',ue.Vector(*position))
    result.set_editor_property('rotation',ue.Quat(x=rotation[0],y=rotation[1],z=rotation[2],w=rotation[3]))
    result.set_editor_property('scale3d',ue.Vector(*scale))
    return result
def source_position(p):return (p[1]*100,p[0]*100,p[2]*100)
def source_rotation(matrix):
    # Blender -> Unreal reflection and the existing +Y hand-socket frame.
    indices=[1,0,2]
    r=[[matrix[i][j] for j in indices] for i in indices]
    trace=sum(r[i][i] for i in range(3))
    if trace>0:
        s=math.sqrt(trace+1)*2
        return ((r[2][1]-r[1][2])/s,(r[0][2]-r[2][0])/s,(r[1][0]-r[0][1])/s,s/4)
    i=max(range(3),key=lambda k:r[k][k]);j=(i+1)%3;k=(i+2)%3
    s=math.sqrt(max(1e-12,1+r[i][i]-r[j][j]-r[k][k]))*2
    q=[0,0,0,0];q[i]=s/4;q[j]=(r[i][j]+r[j][i])/s;q[k]=(r[i][k]+r[k][i])/s;q[3]=(r[k][j]-r[j][k])/s
    return tuple(q)

def retarget(idle,destination,motion,fps,one_handed=False,right_socket='HandGrip_R',weapon_origin=None,weapon_scale=None,motion_basis=(0,0,0,1),preserve_support_hand=False):
    # The existing fitted pose uses a 30Hz compression target. Resample onto
    # that exact grid; half samples at 60Hz can assert in UE's compressor.
    output_fps=30
    if round(fps)!=output_fps:
        last=round((len(motion)-1)*output_fps/fps)
        motion=[motion[min(len(motion)-1,round(i*fps/output_fps))] for i in range(last+1)]
    asset=ue.load_asset(destination) or ue.EditorAssetLibrary.duplicate_asset(idle.get_path_name(),destination)
    options=ue.AnimPoseEvaluationOptions();options.set_editor_property('should_retarget',False)
    options.set_editor_property('optional_skeletal_mesh',ue.load_asset('/Game/EclipseRecoil/Characters/Teams/Alpha/FirstPerson/SK_ER_Twinblast_Alpha_Officer_Forearms'))
    initial=ue.AnimPoseExtensions.get_anim_pose_at_time(idle,0,options)
    bone_names=[str(n) for n in ue.AnimPoseExtensions.get_bone_names(initial)]
    arms={}
    for side in ['r','l']:
        names=['upperarm_'+side,'lowerarm_'+side,'hand_'+side]
        bones=[ue.AnimPoseExtensions.get_bone_pose(initial,n,ue.AnimPoseSpaces.WORLD) for n in names]
        socket=ue.AnimPoseExtensions.get_socket_pose(initial,right_socket if side=='r' else 'HandGrip_L',ue.AnimPoseSpaces.WORLD)
        hand_rotation=quat(bones[2].rotation)
        arms[side]=dict(names=names,bones=bones,position=xyz(socket.translation),rotation=quat(socket.rotation),
            offset=rotate(inverse(hand_rotation),sub(xyz(socket.translation),xyz(bones[2].translation))),
            socket_rotation=qmul(inverse(hand_rotation),quat(socket.rotation)))
    tracks={name:[] for name in bone_names}
    origin=weapon_origin or arms['r']['position']
    origin_offset=sub(origin,arms['r']['position'])
    actual_scale=arms['r']['bones'][2].scale3d.x if weapon_scale is None else weapon_scale
    motion_rotation=lambda matrix:qmul(qmul(motion_basis,source_rotation(matrix)),inverse(motion_basis))
    motion_position=lambda p:mul(rotate(motion_basis,source_position(p)),actual_scale)
    clamps=0;max_contact_error=0
    for frame in motion:
        pose=ue.AnimPoseExtensions.get_anim_pose_at_time(idle,0,options)
        for side,arm in arms.items():
            if side=='l' and preserve_support_hand:continue
            bones=arm['bones'];shoulder,elbow,hand=[xyz(b.translation) for b in bones]
            original_shoulder=shoulder
            root=frame['root']
            delta=frame.get('hands',{}).get(side)
            movement=motion_position([root[i][3] for i in range(3)])
            movement=add(movement,sub(origin_offset,rotate(motion_rotation(root),origin_offset)))
            matrix=root if side=='r' or not delta else delta['delta']
            if side=='l' and delta and 'grip' in delta:
                grip=add(origin,motion_position(delta['grip']))
            else:grip=add(arm['position'],movement)
            grip_rotation=qmul(motion_rotation(matrix),arm['rotation'])
            rotation=qmul(grip_rotation,inverse(arm['socket_rotation']))
            target=sub(grip,rotate(rotation,arm['offset']))
            upper=length(sub(elbow,shoulder));lower=length(sub(hand,elbow))
            distance=length(sub(target,shoulder));direction=unit(sub(target,shoulder))
            bounded=max(abs(upper-lower)+.1,min(upper+lower-.1,distance))
            if abs(bounded-distance)>.01:
                # Upper-arm geometry is excluded from FPS meshes. Translate
                # its hidden shoulder rather than breaking magazine contact.
                shoulder=add(shoulder,mul(direction,distance-bounded));clamps+=1
            distance=bounded
            pole=(-35,10,130) if side=='r' else (35,30,140)
            # The support arm bends toward its fitted idle elbow: a separate pole
            # made that elbow jump at the first frame of every shot and reload.
            if side=='l' or right_socket=='ER_Palm_R':pole=elbow
            pole_direction=sub(pole,shoulder)
            bend=unit(sub(pole_direction,mul(direction,dot(pole_direction,direction))))
            along=(upper*upper-lower*lower+distance*distance)/(2*distance)
            height=math.sqrt(max(0,upper*upper-along*along))
            target_elbow=add(add(shoulder,mul(direction,along)),mul(bend,height))
            rotations=[qmul(from_to(sub(elbow,original_shoulder),sub(target_elbow,shoulder)),quat(bones[0].rotation)),
                       qmul(from_to(sub(hand,elbow),sub(target,target_elbow)),quat(bones[1].rotation)),rotation]
            for name,position,rotation,bone in zip(arm['names'],[shoulder,target_elbow,target],rotations,bones):
                pose=ue.AnimPoseExtensions.set_bone_pose(pose,transform(position,rotation,xyz(bone.scale3d)),name,ue.AnimPoseSpaces.WORLD)
            actual=ue.AnimPoseExtensions.get_socket_pose(pose,right_socket if side=='r' else 'HandGrip_L',ue.AnimPoseSpaces.WORLD)
            max_contact_error=max(max_contact_error,length(sub(xyz(actual.translation),grip)))
        for name in bone_names:
            tracks[name].append(ue.AnimPoseExtensions.get_bone_pose(pose,name,ue.AnimPoseSpaces.LOCAL))
    controller=asset.get_editor_property('controller')
    controller.open_bracket('Retarget supplied weapon hand motion',False)
    try:
        controller.set_frame_rate(ue.FrameRate(numerator=output_fps,denominator=1),False)
        controller.set_number_of_frames(ue.FrameNumber(value=max(1,len(motion)-1)),False)
        existing=set(str(n) for n in controller.get_model_interface().get_bone_track_names())
        for name,keys in tracks.items():
            if name not in existing:continue
            if not controller.set_bone_track_keys(name,[k.translation for k in keys],[k.rotation for k in keys],[k.scale3d for k in keys],False):
                raise RuntimeError('Hand track failed '+name)
    finally:controller.close_bracket(False)
    asset.set_editor_property('enable_root_motion',False)
    if max_contact_error>.05:raise RuntimeError('Retargeted palm misses contact: '+str(max_contact_error)+' cm '+destination)
    if not ue.EditorAssetLibrary.save_loaded_asset(asset,only_if_is_dirty=False):raise RuntimeError('Cannot save '+destination)
    ue.log('ER_REFERENCE_HANDS '+destination+' frames='+str(len(motion))+' shoulder_reach_adjustments='+str(clamps)+' max_contact_error_cm='+str(max_contact_error))
    return asset
