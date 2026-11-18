"""Fit the original static pistol pose to the third-person weapon profile.

The source contains arms and the pistol. Its first frame agrees with the
reload endpoints. Keep the game's world rig, aiming and obstacle clearance.
"""
from pathlib import Path
import hashlib
import json
import runpy
import zipfile

import unreal as ue

project = Path(ue.Paths.project_dir()).resolve()
assets = ue.EditorAssetLibrary
folder = '/Game/EclipseRecoil/Weapons/Sketchfab/Pistol'
data = ue.load_asset(folder + '/DA_Pistol')
source_mesh = ue.load_asset(folder + '/OriginalPreview/SK_PistolOriginal')
source = ue.load_asset(folder + '/OriginalPreview/SK_PistolOriginalPistolOriginal_Original_Full')
if not all((data, source_mesh, source)):
    raise RuntimeError('Missing original pistol preview')

geometry = runpy.run_path(str(project / 'scripts/palm_geometry.py'))

def evaluate(clip, mesh, time=0.):
    options = ue.AnimPoseEvaluationOptions()
    options.should_retarget = False
    options.optional_skeletal_mesh = mesh
    pose = ue.AnimPoseExtensions.get_anim_pose_at_time(clip, time, options)
    if not ue.AnimPoseExtensions.is_valid(pose):
        raise RuntimeError('Could not evaluate ' + clip.get_path_name())
    return pose

def bone(pose, name):
    return ue.AnimPoseExtensions.get_bone_pose(pose, name, ue.AnimPoseSpaces.WORLD)

def rigid(frame):
    return ue.Transform(location=frame.translation, rotation=frame.rotation.rotator())

def xyz(vector):
    return [vector.x, vector.y, vector.z]

rest = evaluate(source, source_mesh)
weapon = evaluate(data.weapon_idle_animation, data.mesh)
source_to_weapon = ue.MathLibrary.compose_transforms(
    ue.MathLibrary.invert_transform(rigid(bone(rest, 'P_base'))), rigid(bone(weapon, 'P_base')))

def world_to_weapon(point):
    return ue.MathLibrary.transform_location(source_to_weapon, point)

palms = {}
elbow_poles = {}
shoulders = []
lengths = []
for side in ('L', 'R'):
    prefix = f'B_{side}_'
    wrist_name, palm_name = prefix + 'wrist', prefix + 'palm'
    wrist_bind = ue.ERCharacter.get_weapon_reference_bone(source_mesh, wrist_name)
    palm_bind = ue.ERCharacter.get_weapon_reference_bone(source_mesh, palm_name)
    surface = [xyz(v) for v in ue.ERCharacter.get_presentation_vertices(source_mesh, wrist_name)]
    for point in ue.ERCharacter.get_presentation_vertices(source_mesh, palm_name):
        surface.append(xyz(ue.MathLibrary.inverse_transform_location(
            wrist_bind, ue.MathLibrary.transform_location(palm_bind, point))))
    fingers = {}
    for name, source_name in (('thumb', 'thumb'), ('index', 'point'), ('middle', 'middle'),
                              ('ring', 'ring'), ('pinky', 'pink')):
        knuckle = ue.ERCharacter.get_weapon_reference_bone(source_mesh, prefix + source_name + '1')
        fingers[name] = xyz(ue.MathLibrary.inverse_transform_location(wrist_bind, knuckle.translation))
    palm = geometry['palm_surface'](surface, fingers)
    frame = ue.Transform(location=ue.Vector(*palm['position']),
                         rotation=ue.MathLibrary.make_rot_from_xz(ue.Vector(*palm['x']), ue.Vector(*palm['z'])))
    wrist_pose = bone(rest, wrist_name)
    palms[side] = ue.MathLibrary.compose_transforms(
        ue.MathLibrary.compose_transforms(frame, wrist_pose), source_to_weapon)
    shoulder, elbow, wrist = [world_to_weapon(bone(rest, prefix + name).translation)
                              for name in ('arm', 'elbow', 'wrist')]
    direction = (wrist - shoulder) * (1. / (wrist - shoulder).length())
    bend = elbow - shoulder
    pole = bend - direction * sum(a * b for a, b in zip(xyz(bend), xyz(direction)))
    elbow_poles[side] = pole * (1. / pole.length())
    shoulders.append(shoulder)
    lengths.append((elbow - shoulder).length() + (wrist - elbow).length())

ready = (shoulders[0] + shoulders[1]) * (-1. / sum(lengths))
values = dict(
    world_right_palm_contact=palms['R'].translation,
    world_right_palm_rotation=palms['R'].rotation.rotator(),
    world_pistol_support_contact=palms['L'].translation,
    world_left_palm_rotation=palms['L'].rotation.rotator(),
    world_pistol_ready_offset=ready,
    world_pistol_right_elbow_pole=elbow_poles['R'],
    world_pistol_left_elbow_pole=elbow_poles['L'],
)
for side, palm in palms.items():
    if palm.translation.length() > 24. or elbow_poles[side].length() < .99:
        raise RuntimeError('Invalid source world pose for ' + side)
if not .7 < ready.x < 1.3:
    raise RuntimeError('Source weapon axis does not match the ready pose')

backup = project / 'Saved/PistolWorldSourcePose'
backup.mkdir(parents=True, exist_ok=True)
package = project / 'Content/EclipseRecoil/Weapons/Sketchfab/Pistol/DA_Pistol.uasset'
owned = package.resolve()
protected = {p.resolve(): hashlib.sha256(p.read_bytes()).hexdigest()
             for subdir in ('characters', 'weapons', 'maps')
             for p in (project / 'Assets' / subdir / 'Content').rglob('*.uasset')
             if p.resolve() != owned}
archive = backup / 'before.zip'
if not archive.exists():
    with zipfile.ZipFile(archive, 'x', zipfile.ZIP_DEFLATED) as saved:
        saved.write(package, package.name)
    with zipfile.ZipFile(archive) as saved:
        if saved.testzip() or saved.read(package.name) != package.read_bytes():
            raise RuntimeError('Pistol world pose backup failed')

for key, value in values.items():
    data.set_editor_property(key, value)
assets.set_metadata_tag(data, 'ERPistolWorldSourcePose', '1')
if not assets.save_loaded_asset(data, False):
    raise RuntimeError('Cannot save pistol world pose')
for path, digest in protected.items():
    if hashlib.sha256(path.read_bytes()).hexdigest() != digest:
        raise RuntimeError('Protected package changed: ' + str(path))
report = dict(ready_offset=xyz(ready), elbow_poles={key: xyz(value) for key, value in elbow_poles.items()},
              palm_positions_cm={key: xyz(value.translation) for key, value in palms.items()},
              source=source.get_path_name(), protected_packages=len(protected))
(backup / 'receipt.json').write_text(json.dumps(report, indent=2) + '\n')
ue.log('ER_PISTOL_WORLD_SOURCE_READY ' + json.dumps(report))
