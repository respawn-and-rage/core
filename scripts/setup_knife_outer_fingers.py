"""Relax the outer edge of the knife grip on the shared first-person rig."""
from pathlib import Path
import hashlib
import json
import math
import runpy
import zipfile

import unreal as ue


def apply():
    root = Path(ue.Paths.project_dir()).resolve()
    folder = '/Game/EclipseRecoil/Weapons/Sketchfab/Knife'
    directory = root / 'Content' / folder[6:]
    data = ue.load_asset(folder + '/DA_Knife')
    clips = (data.idle_animation, data.fire_animation)
    if [clip.get_name() for clip in clips] != ['A_Knife_Hands_ManualReady', 'A_Knife_Hands_ManualStab']:
        raise RuntimeError('Expected manual knife clips')
    owned = {directory / (clip.get_name() + '.uasset') for clip in clips}
    protected = {p: hashlib.sha256(p.read_bytes()).digest() for p in directory.rglob('*.uasset') if p not in owned and p.name != "DA_Knife.uasset"}
    backup = root / 'Saved/KnifeOuterFingers/before.zip'
    backup.parent.mkdir(parents=True, exist_ok=True)
    if not backup.exists():
        with zipfile.ZipFile(backup, 'x', zipfile.ZIP_DEFLATED) as archive:
            for p in owned:
                archive.write(p, p.name)
        with zipfile.ZipFile(backup) as archive:
            if any(archive.read(p.name) != p.read_bytes() for p in owned):
                raise RuntimeError('Knife clip backup failed')

    alpha = '/Game/EclipseRecoil/Characters/Teams/Alpha/FirstPerson'
    options = ue.AnimPoseEvaluationOptions()
    options.optional_skeletal_mesh = ue.load_asset(alpha + '/SK_ER_Twinblast_Alpha_Officer_Forearms')
    options.should_retarget = False
    options.evaluation_type = ue.AnimDataEvalType.RAW
    ext = ue.AnimPoseExtensions
    local, world = ue.AnimPoseSpaces.LOCAL, ue.AnimPoseSpaces.WORLD
    source = ue.load_asset(folder + '/KnifeGripEditing/A_Knife_ManualGrip')
    editing = folder + '/KnifeGripEditing'
    baseline = ue.load_asset(editing + '/A_Knife_ManualBaseline')
    motion = ue.load_asset(editing + '/A_Knife_ManualMotionReference')
    h = runpy.run_path(str(root / 'scripts/retarget_weapon_hands.py'))
    frame = json.loads(ue.EditorAssetLibrary.get_metadata_tag(source, 'EROriginalKnifeFrame'))
    attachment = h['transform'](frame['p'], frame['q'], frame['s'])
    pose = ext.get_anim_pose_at_time(baseline, 0, options)
    grip = ue.MathLibrary.compose_transforms(attachment, ext.get_socket_pose(pose, 'ER_Palm_R', world))
    pommel = ue.Vector(*h['rotate'](h['quat'](grip.rotation), (0, 1, 0)))
    targets = {}
    for finger, distance, relaxation in [('ring', .75, .27), ('pinky', 1.15, .38)]:
        meta = finger + '_metacarpal_r'
        first = finger + '_01_r'
        metacarpal = ext.get_bone_pose(pose, meta, world)
        ray = ext.get_bone_pose(pose, first, world).translation - metacarpal.translation
        turn = h['from_to'](h['xyz'](ray), h['xyz'](ray + pommel * distance))
        metacarpal.set_editor_property('rotation', ue.Quat(*h['qmul'](turn, h['quat'](metacarpal.rotation))))
        pose = ext.set_bone_pose(pose, metacarpal, meta, world)
        targets[meta] = ext.get_bone_pose(pose, meta, local)
        proximal = ext.get_bone_pose(pose, first, local)
        neutral = ext.get_ref_bone_pose(pose, first, local)
        proximal.set_editor_property('rotation', ue.MathLibrary.quat_slerp(proximal.rotation, neutral.rotation, relaxation))
        targets[first] = proximal

    # The fitted distal index joint folds back over the glove. Bound its
    # rotation from bind while keeping the fitted knuckle and middle joint.
    tip_name = 'index_03_r'
    tip = ext.get_bone_pose(pose, tip_name, local)
    neutral_tip = ext.get_ref_bone_pose(pose, tip_name, local)
    angle = ue.MathLibrary.quat_angular_distance(neutral_tip.rotation, tip.rotation)
    if angle > 0:
        tip.set_editor_property('rotation', ue.MathLibrary.quat_slerp(
            neutral_tip.rotation, tip.rotation, min(1., math.radians(65.) / angle)))
    targets[tip_name] = tip

    # The manual wrist was rotated independently of the handle. Restore the
    # paired wrist and attachment so the palm follows the fitted handle again.
    for clip, reference in zip(clips, (baseline, motion)):
        controller = clip.get_editor_property('controller')
        count = controller.get_model_interface().get_number_of_frames() + 1
        controller.open_bracket('Relax the ring and little finger on the knife handle', False)
        try:
            wrists = [ext.get_bone_pose(ext.get_anim_pose_at_time(reference, i / 30, options), 'hand_r', local)
                      for i in range(count)]
            if not controller.set_bone_track_keys('hand_r', [w.translation for w in wrists],
                    [w.rotation for w in wrists], [w.scale3d for w in wrists], False):
                raise RuntimeError('Cannot restore fitted wrist')
            for name, target in targets.items():
                if not controller.set_bone_track_keys(name, [target.translation]*count, [target.rotation]*count, [target.scale3d]*count, False):
                    raise RuntimeError('Cannot write ' + name)
        finally:
            controller.close_bracket(False)
        if not ue.EditorAssetLibrary.save_loaded_asset(clip, False):
            raise RuntimeError('Cannot save ' + clip.get_name())

    data.set_editor_property('first_person_grip_transform', attachment)
    if not ue.EditorAssetLibrary.save_loaded_asset(data, False):
        raise RuntimeError('Cannot save fitted knife attachment')
    options.should_retarget = True
    options.evaluation_type = ue.AnimDataEvalType.COMPRESSED
    skins = ue.EditorAssetLibrary.list_assets(alpha, recursive=False)
    for path in skins:
        options.optional_skeletal_mesh = ue.load_asset(path)
        for clip, reference in zip(clips, (baseline, motion)):
            for t in (0., clip.get_play_length()/2, clip.get_play_length()):
                evaluated = ext.get_anim_pose_at_time(clip, t, options)
                original = ext.get_anim_pose_at_time(reference, t, options)
                palm = ext.get_socket_pose(evaluated, 'ER_Palm_R', world)
                expected = ext.get_socket_pose(original, 'ER_Palm_R', world)
                if (palm.translation - expected.translation).length() > .05 or abs(
                        h['dot'](h['quat'](palm.rotation), h['quat'](expected.rotation))) < .9999:
                    raise RuntimeError(f'Knife palm frame differs on {path} at {t}')
                for name, target in targets.items():
                    actual = ext.get_bone_pose(evaluated, name, local)
                    if abs(h['dot'](h['quat'](actual.rotation), h['quat'](target.rotation))) < .999:
                        raise RuntimeError(f'Finger rotation mismatch on {path}: {name}')
    if not skins or any(hashlib.sha256(p.read_bytes()).digest() != digest for p, digest in protected.items()):
        raise RuntimeError('Knife asset verification failed')
    ue.log(f'ER_KNIFE_OUTER_FINGERS PASS skins={len(skins)}')


if __name__ == '__main__':
    apply()
