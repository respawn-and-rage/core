"""Open an isolated manual grip pose with a stationary pistol reference.

Run in the graphical Unreal Editor with '-ExecCmds=py THIS'. Reopening
never overwrites the user's editing copy. Runtime data stays untouched.
"""
from pathlib import Path
import json
import unreal as ue

assets = ue.EditorAssetLibrary
# The existing native preview helper requires this legacy folder segment.
folder = '/Game/EclipseRecoil/Weapons/Sketchfab/Pistol/KnifeGripEditing'
data = ue.load_asset('/Game/EclipseRecoil/Weapons/Sketchfab/Pistol/DA_Pistol')
manual = ue.load_asset(folder+'/A_Pistol_ManualGrip')
if not manual:
    baseline = assets.duplicate_asset(data.idle_animation.get_path_name(),folder+'/A_Pistol_ManualBaseline')
    manual = assets.duplicate_asset(data.idle_animation.get_path_name(),folder+'/A_Pistol_ManualGrip')
    if not baseline or not manual:raise RuntimeError('Cannot create isolated manual poses')
    for clip in (baseline,manual):
        if not assets.save_loaded_asset(clip,False):raise RuntimeError('Cannot save manual pose')

preview_path = '/Game/EclipseRecoil/Characters/FirstPerson/KnifeGripEditing/Pistol/SK_ManualGripPreview'
preview = ue.load_asset(preview_path)
if not preview:
    skin = ue.load_asset('/Game/EclipseRecoil/Characters/Teams/Alpha/FirstPerson/SK_ER_Belica_Alpha_BelicaArmor_Forearms')
    preview = assets.duplicate_asset(skin.get_path_name(),preview_path)
if not preview:raise RuntimeError('Missing isolated preview skin')
options = ue.AnimPoseEvaluationOptions()
options.optional_skeletal_mesh = preview
options.should_retarget = False
baseline = ue.load_asset(folder+'/A_Pistol_ManualBaseline')
pose = ue.AnimPoseExtensions.get_anim_pose_at_time(baseline,0,options)
parent = ue.AnimPoseExtensions.get_socket_pose(pose,'ER_Palm_R',ue.AnimPoseSpaces.WORLD)
frame = data.first_person_grip_transform
saved_frame = assets.get_metadata_tag(manual,'EROriginalPistolFrame')
if saved_frame:
    f = json.loads(saved_frame)
    frame = ue.Transform(location=ue.Vector(*f['p']),scale=ue.Vector(*f['s']))
    frame.set_editor_property('rotation',ue.Quat(x=f['q'][0],y=f['q'][1],z=f['q'][2],w=f['q'][3]))
weapon = ue.MathLibrary.compose_transforms(frame,parent)
weapon.set_editor_property('scale3d',frame.scale3d)
root = ue.AnimPoseExtensions.get_bone_pose(pose,'root',ue.AnimPoseSpaces.WORLD)
relative = ue.MathLibrary.make_relative_transform(weapon,root)
if not ue.ERCharacter.configure_knife_grip_editor(preview,manual,data.mesh,relative):
    raise RuntimeError('Cannot configure stationary pistol preview')
for asset in (preview,manual):
    if not assets.save_loaded_asset(asset,False):raise RuntimeError('Cannot save editing preview')
ue.get_editor_subsystem(ue.AssetEditorSubsystem).open_editor_for_assets([manual])
ue.log('ER_PISTOL_MANUAL_EDITOR_READY '+manual.get_path_name())
