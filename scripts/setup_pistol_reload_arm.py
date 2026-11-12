"""Tuck the left elbow in the owned first-person pistol reload clip."""
from pathlib import Path
import hashlib
import runpy
import shutil

import unreal as ue

root = Path(ue.Paths.project_dir()).resolve()
math_tools = runpy.run_path(str(root / 'scripts/retarget_weapon_hands.py'))
add, sub, mul, dot, unit, length, xyz, quat, qmul, from_to, transform = (
    math_tools[name] for name in
    ('add', 'sub', 'mul', 'dot', 'unit', 'length', 'xyz', 'quat', 'qmul', 'from_to', 'transform')
)
asset_dir = '/Game/EclipseRecoil/Weapons/Sketchfab/Pistol'
files = root / 'Content/EclipseRecoil/Weapons/Sketchfab/Pistol'
clip_name = 'A_Pistol_Hands_Reload'
marker = 'ERPistolReloadArmRevision'
bones = ('upperarm_l', 'lowerarm_l', 'hand_l')


def smoothstep(value):
    value = max(0., min(1., value))
    return value * value * (3. - 2. * value)


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def apply():
    data = ue.load_asset(f'{asset_dir}/DA_Pistol')
    clip = data.get_editor_property('reload_animation')
    if clip.get_path_name() != f'{asset_dir}/{clip_name}.{clip_name}':
        raise RuntimeError('Unexpected pistol reload clip')
    assets = ue.EditorAssetLibrary
    if assets.get_metadata_tag(clip, marker) == '1':
        ue.log('ER_PISTOL_RELOAD_ARM already applied')
        return

    owned = files / f'{clip_name}.uasset'
    protected = {file: digest(file) for file in files.rglob('*.uasset') if file != owned}
    backup_dir = root / 'Saved/Testing/PistolReloadArmBackup'
    backup_dir.mkdir(parents=True, exist_ok=True)
    before = digest(owned)
    backup = backup_dir / f'{clip_name}-{before}.uasset'
    if not backup.exists():
        shutil.copy2(owned, backup)
    if digest(backup) != before:
        raise RuntimeError('Pistol reload backup mismatch')

    options = ue.AnimPoseEvaluationOptions()
    options.should_retarget = False
    options.evaluation_type = ue.AnimDataEvalType.RAW
    options.optional_skeletal_mesh = ue.load_asset(
        '/Game/EclipseRecoil/Characters/Teams/Alpha/FirstPerson/SK_ER_Twinblast_Alpha_Officer_Forearms')
    controller = clip.get_editor_property('controller')
    model = controller.get_model_interface()
    rate = model.get_frame_rate()
    duration = clip.get_play_length()
    times = [min(duration, index * rate.denominator / rate.numerator)
             for index in range(model.get_number_of_frames() + 1)]
    tracks = {name: [] for name in bones}
    expected = []
    # In rig space +X is camera-left, +Y is forward, and +Z is up.
    tucked_direction = unit((.30, -.55, -1.))
    fade_seconds = .30
    for time in times:
        pose = ue.AnimPoseExtensions.get_anim_pose_at_time(clip, time, options)
        frames = [ue.AnimPoseExtensions.get_bone_pose(pose, name, ue.AnimPoseSpaces.WORLD)
                  for name in bones]
        shoulder, elbow, wrist = [xyz(frame.translation) for frame in frames]
        weight = smoothstep(min(time, duration - time) / fade_seconds)
        # Rotate the elbow around the shoulder-to-wrist axis. This preserves
        # both segment lengths and uses the existing shoulder translation.
        axis = unit(sub(wrist, shoulder))
        center = add(shoulder, mul(axis, dot(sub(elbow, shoulder), axis)))
        bend = sub(elbow, center)
        tucked_bend = sub(tucked_direction, mul(axis, dot(tucked_direction, axis)))
        direction = unit(add(mul(unit(bend), 1. - weight), mul(unit(tucked_bend), weight)))
        new_elbow = add(center, mul(direction, length(bend))) if weight > 0. else elbow
        if weight > 0.:
            upper_rotation = qmul(from_to(sub(elbow, shoulder), sub(new_elbow, shoulder)),
                                  quat(frames[0].rotation))
            lower_rotation = qmul(from_to(sub(wrist, elbow), sub(wrist, new_elbow)),
                                  quat(frames[1].rotation))
            for name, position, rotation, frame in zip(
                bones, (shoulder, new_elbow, wrist),
                (upper_rotation, lower_rotation, quat(frames[2].rotation)), frames
            ):
                pose = ue.AnimPoseExtensions.set_bone_pose(
                    pose, transform(position, rotation, xyz(frame.scale3d)), name, ue.AnimPoseSpaces.WORLD)
        for name in bones:
            tracks[name].append(ue.AnimPoseExtensions.get_bone_pose(pose, name, ue.AnimPoseSpaces.LOCAL))
        expected.append((new_elbow, wrist, quat(frames[2].rotation)))

    controller.open_bracket('Tuck pistol reload support arm', False)
    try:
        for name, keys in tracks.items():
            if not controller.set_bone_track_keys(
                name, [key.translation for key in keys], [key.rotation for key in keys],
                [key.scale3d for key in keys], False
            ):
                raise RuntimeError(f'Cannot write pistol reload track {name}')
    finally:
        controller.close_bracket(False)

    for time, (elbow, wrist, hand_rotation) in zip(times, expected):
        pose = ue.AnimPoseExtensions.get_anim_pose_at_time(clip, time, options)
        lower = ue.AnimPoseExtensions.get_bone_pose(pose, bones[1], ue.AnimPoseSpaces.WORLD)
        hand = ue.AnimPoseExtensions.get_bone_pose(pose, bones[2], ue.AnimPoseSpaces.WORLD)
        rotation_dot = abs(sum(a * b for a, b in zip(hand_rotation, quat(hand.rotation))))
        if (length(sub(xyz(lower.translation), elbow)) > .01
                or length(sub(xyz(hand.translation), wrist)) > .01
                or rotation_dot < .999999):
            raise RuntimeError(f'Pistol reload pose mismatch at {time:.3f}s')
    assets.set_metadata_tag(clip, marker, '1')
    if not assets.save_loaded_asset(clip, False):
        raise RuntimeError('Cannot save pistol reload clip')
    for file, original in protected.items():
        if digest(file) != original:
            raise RuntimeError(f'Protected pistol asset changed: {file}')
    ue.log(f'ER_PISTOL_RELOAD_ARM PASS frames={len(times)}')


apply()
