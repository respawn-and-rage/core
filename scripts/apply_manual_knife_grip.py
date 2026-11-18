"""Persist the user's editor bone correction and apply it along the existing stab.

The editor's unkeyed wrist rotation was recovered from its numeric Bone panel.
Keep the knife transform, all other bone edits, source clips and gameplay intact.
"""
from pathlib import Path
import hashlib
import json
import runpy
import zipfile
import unreal as ue


def apply(settings=None):
    root = Path(ue.Paths.project_dir()).resolve()
    folder = '/Game/EclipseRecoil/Weapons/Sketchfab/Knife'
    editing = folder+'/KnifeGripEditing'
    assets = ue.EditorAssetLibrary
    data = ue.load_asset(folder+'/DA_Knife')
    manual = ue.load_asset(editing+'/A_Knife_ManualGrip')
    baseline = ue.load_asset(editing+'/A_Knife_ManualBaseline')
    skin = ue.load_asset('/Game/EclipseRecoil/Characters/FirstPerson/KnifeGripEditing/SK_ManualGripPreview')
    if not all((data,manual,baseline,skin)):raise RuntimeError('Missing manual editing assets')
    h = runpy.run_path(str(root/'scripts/retarget_weapon_hands.py'))
    xyz,quat = h['xyz'],h['quat']
    qmul,inv,rotate = h['qmul'],h['inverse'],h['rotate']
    private = root/'Assets/weapons/SourceAssets/SketchfabWeapons'
    backup = private/'KnifeManualGripBeforeActivation.zip'
    if not backup.exists():
        files = [root/('Content/'+a.get_path_name().split('.')[0].removeprefix('/Game/')+'.uasset')
                 for a in (data,data.idle_animation,data.fire_animation,manual,baseline)]
        with zipfile.ZipFile(backup,'x',zipfile.ZIP_DEFLATED) as archive:
            hashes = {}
            for file in files:
                content = file.read_bytes();name = str(file.relative_to(root))
                hashes[name] = hashlib.sha256(content).hexdigest();archive.writestr(name,content)
            archive.writestr('manifest.json',json.dumps(hashes))
        with zipfile.ZipFile(backup) as archive:
            if archive.testzip() or any(hashlib.sha256(archive.read(n)).hexdigest()!=v for n,v in hashes.items()):
                raise RuntimeError('Manual activation backup verification failed')
    source = ue.load_asset(editing+'/A_Knife_ManualMotionReference')
    if not source:
        source = assets.duplicate_asset(data.fire_animation.get_path_name(),editing+'/A_Knife_ManualMotionReference')
        assets.save_loaded_asset(source,False)
    options = ue.AnimPoseEvaluationOptions();options.should_retarget=False
    options.optional_skeletal_mesh=skin;options.evaluation_type=ue.AnimDataEvalType.RAW
    world,local = ue.AnimPoseSpaces.WORLD,ue.AnimPoseSpaces.LOCAL
    evaluate = lambda a,t=0:ue.AnimPoseExtensions.get_anim_pose_at_time(a,t,options)
    bone = lambda p,n,s=world:ue.AnimPoseExtensions.get_bone_pose(p,n,s)
    palm = lambda p:ue.AnimPoseExtensions.get_socket_pose(p,'ER_Palm_R',world)
    bp,mp = evaluate(baseline),evaluate(manual)
    approved = assets.get_metadata_tag(manual,'ERUserBonePoseRecorded')=='1'
    names = [str(n) for n in ue.AnimPoseExtensions.get_bone_names(bp)]
    changed = []
    for n in names:
        a,b = bone(bp,n,local),bone(mp,n,local)
        if (a.translation-b.translation).length()>.001 or abs(h['dot'](quat(a.rotation),quat(b.rotation)))<.999999 or (a.scale3d-b.scale3d).length()>.001:
            changed.append(n)
    if not changed and not approved:
        # Exact local-space fields visible in the user's saved editor session:
        # Rotation X=Roll, Y=Pitch, Z=Yaw. Position/scale were unchanged.
        hand = bone(mp,'hand_r',local)
        hand.set_editor_property('rotation',ue.Rotator(pitch=-10.9159,yaw=-8.93675,roll=-46.2194).quaternion())
        mp = ue.AnimPoseExtensions.set_bone_pose(mp,hand,'hand_r',local)
        changed = ['hand_r']
    if not changed:raise RuntimeError('No user bone correction to activate')
    protected = {p:data.get_editor_property(p) for p in ('mesh','weapon_idle_animation','weapon_fire_animation','view_offset','grip_transform','damage','hitscan_range','shot_interval','payload')}
    original_frame_text = assets.get_metadata_tag(manual,'EROriginalKnifeFrame')
    if original_frame_text:
        f = json.loads(original_frame_text)
        original_frame = h['transform'](f['p'],f['q'],f['s'])
    else:
        original_frame = data.first_person_grip_transform
        assets.set_metadata_tag(manual,'EROriginalKnifeFrame',json.dumps(dict(p=xyz(original_frame.translation),q=quat(original_frame.rotation),s=xyz(original_frame.scale3d))))
    weapon_scale = original_frame.scale3d
    def weapon(p,frame):
        t = ue.MathLibrary.compose_transforms(frame,palm(p));t.set_editor_property('scale3d',weapon_scale);return t
    w0 = weapon(bp,original_frame)
    new_frame = ue.MathLibrary.make_relative_transform(w0,palm(mp));new_frame.set_editor_property('scale3d',weapon_scale)
    def write(asset,poses):
        controller=asset.get_editor_property('controller');controller.open_bracket('Persist user knife bone pose',False)
        try:
            controller.set_frame_rate(ue.FrameRate(numerator=30,denominator=1),False)
            controller.set_number_of_frames(ue.FrameNumber(value=len(poses)-1),False)
            for n in names:
                keys=[bone(p,n,local) for p in poses]
                if not controller.set_bone_track_keys(n,[k.translation for k in keys],[k.rotation for k in keys],[k.scale3d for k in keys],False):raise RuntimeError('Cannot write '+n)
        finally:controller.close_bracket(False)
        if not assets.save_loaded_asset(asset,False):raise RuntimeError('Cannot save manual animation')
    write(manual,[mp,mp]);assets.set_metadata_tag(manual,'ERUserBonePoseRecorded','1');assets.save_loaded_asset(manual,False)
    ready = ue.load_asset(folder+'/A_Knife_Hands_ManualReady') or assets.duplicate_asset(manual.get_path_name(),folder+'/A_Knife_Hands_ManualReady')
    fire = ue.load_asset(folder+'/A_Knife_Hands_ManualStab') or assets.duplicate_asset(source.get_path_name(),folder+'/A_Knife_Hands_ManualStab')
    write(ready,[mp,mp])
    poses=[];max_error=0.
    # Use the archived gesture, never an already corrected runtime clip.
    count=source.get_editor_property('controller').get_model_interface().get_number_of_frames()+1
    for i in range(count):
        p=evaluate(source,i/30);old_weapon=weapon(p,original_frame)
        delta=qmul(quat(old_weapon.rotation),inv(quat(w0.rotation)))
        for n in changed:
            if n in ('upperarm_r','lowerarm_r','hand_r','clavicle_r'):
                a,b=bone(bp,n),bone(mp,n);old=bone(p,n)
                offset=rotate(delta,h['sub'](xyz(b.translation),xyz(a.translation)))
                correction=qmul(qmul(delta,qmul(quat(b.rotation),inv(quat(a.rotation)))),inv(delta))
                target=h['transform'](h['add'](xyz(old.translation),offset),qmul(correction,quat(old.rotation)),xyz(b.scale3d))
                p=ue.AnimPoseExtensions.set_bone_pose(p,target,n,world)
            else:p=ue.AnimPoseExtensions.set_bone_pose(p,bone(mp,n,local),n,local)
        actual=weapon(p,new_frame)
        max_error=max(max_error,(actual.translation-old_weapon.translation).length())
        if max_error>.02 or abs(h['dot'](quat(actual.rotation),quat(old_weapon.rotation)))<.99999:
            raise RuntimeError('User pose changed knife trajectory')
        poses.append(p)
    write(fire,poses)
    options.evaluation_type=ue.AnimDataEvalType.COMPRESSED
    expected_hand=bone(evaluate(manual),'hand_r',local)
    for asset,time in ((ready,0),(fire,0),(fire,fire.get_play_length())):
        actual_hand=bone(evaluate(asset,time),'hand_r',local)
        if abs(h['dot'](quat(expected_hand.rotation),quat(actual_hand.rotation)))<.99999:
            raise RuntimeError('Compressed clip lost the user wrist rotation')
    rp=evaluate(ready)
    for n in names:
        if n in changed:continue
        a,b=bone(bp,n,local),bone(rp,n,local)
        if (a.translation-b.translation).length()>.01 or abs(h['dot'](quat(a.rotation),quat(b.rotation)))<.99999 or (a.scale3d-b.scale3d).length()>.001:
            raise RuntimeError('Unexpected change outside user bones: '+n)
    data.set_editor_property('idle_animation',ready);data.set_editor_property('fire_animation',fire)
    data.set_editor_property('first_person_grip_transform',new_frame)
    assets.set_metadata_tag(data,'ERKnifeManualGripApproved','1')
    if protected!={p:data.get_editor_property(p) for p in protected}:raise RuntimeError('Protected knife fields changed')
    if not assets.save_loaded_asset(data,False):raise RuntimeError('Cannot activate user grip')
    runpy.run_path(str(root/'scripts/setup_knife_outer_fingers.py'))['apply']()
    if settings is not None:
        settings.update(idle_animation=ready,fire_animation=fire,first_person_palm_grip=data.first_person_palm_grip,
                        first_person_grip_transform=data.first_person_grip_transform,view_offset=data.view_offset,
                        weapon_idle_animation=data.weapon_idle_animation,weapon_fire_animation=data.weapon_fire_animation)
    angles=bone(evaluate(ready),'hand_r',local).rotation.rotator()
    receipt=dict(manual_edit=True,changed_bones=changed,hand_local_rotation_degrees=dict(roll=angles.roll,pitch=angles.pitch,yaw=angles.yaw),knife_trajectory_error_cm=max_error,
                 idle=ready.get_path_name(),fire=fire.get_path_name(),range_cm=data.hitscan_range,seconds=fire.get_play_length())
    (private/'knife-manual-grip.json').write_text(json.dumps(receipt,indent=2)+'\n')
    ue.log('ER_MANUAL_KNIFE_ACTIVATED '+json.dumps(receipt))
    return receipt


if __name__=='__main__':apply()
