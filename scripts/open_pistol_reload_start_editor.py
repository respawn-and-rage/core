"""Open an isolated, static copy of the runtime pistol reload's first frame.

Existing user edits are never overwritten. Only the new editing pose, baseline
and preview mesh are saved; runtime data and motion clips remain unchanged.
"""
from pathlib import Path
import hashlib
import unreal as ue

assets=ue.EditorAssetLibrary
root=Path(ue.Paths.project_dir()).resolve()
folder='/Game/EclipseRecoil/Weapons/Sketchfab/Pistol/KnifeGripEditing'
data=ue.load_asset('/Game/EclipseRecoil/Weapons/Sketchfab/Pistol/DA_Pistol')
source=data.reload_animation
owned=[data,source,data.idle_animation,data.fire_animation]
def file(a):return root/('Content/'+a.get_path_name().split('.')[0].removeprefix('/Game/')+'.uasset')
protected={file(a):hashlib.sha256(file(a).read_bytes()).hexdigest() for a in owned}
manual=ue.load_asset(folder+'/A_Pistol_ReloadStart_Manual')
preview_path='/Game/EclipseRecoil/Characters/FirstPerson/KnifeGripEditing/PistolReloadStart/SK_ReloadStart_Preview'
preview=ue.load_asset(preview_path)
if not preview:
    skin=ue.load_asset('/Game/EclipseRecoil/Characters/Teams/Alpha/FirstPerson/SK_ER_Belica_Alpha_BelicaArmor_Forearms')
    preview=assets.duplicate_asset(skin.get_path_name(),preview_path)
if not preview:raise RuntimeError('Cannot create reload-start preview')
options=ue.AnimPoseEvaluationOptions();options.should_retarget=False
options.optional_skeletal_mesh=preview
options.evaluation_type=ue.AnimDataEvalType.COMPRESSED
pose=ue.AnimPoseExtensions.get_anim_pose_at_time(source,0.,options)
if not manual:
    baseline=assets.duplicate_asset(source.get_path_name(),folder+'/A_Pistol_ReloadStart_Baseline')
    manual=assets.duplicate_asset(source.get_path_name(),folder+'/A_Pistol_ReloadStart_Manual')
    for clip in (baseline,manual):
        if not clip:raise RuntimeError('Cannot create reload-start editing pose')
        controller=clip.get_editor_property('controller')
        controller.open_bracket('Static runtime pistol reload start',False)
        try:
            controller.set_frame_rate(ue.FrameRate(numerator=30,denominator=1),False)
            controller.set_number_of_frames(ue.FrameNumber(value=1),False)
            for n in ue.AnimPoseExtensions.get_bone_names(pose):
                t=ue.AnimPoseExtensions.get_bone_pose(pose,n,ue.AnimPoseSpaces.LOCAL)
                if not controller.set_bone_track_keys(n,[t.translation]*2,[t.rotation]*2,[t.scale3d]*2,False):raise RuntimeError('Cannot freeze '+str(n))
        finally:controller.close_bracket(False)
        if not assets.save_loaded_asset(clip,False):raise RuntimeError('Cannot save static editing pose')
parent=ue.AnimPoseExtensions.get_socket_pose(pose,'ER_Palm_R',ue.AnimPoseSpaces.WORLD)
frame=data.first_person_grip_transform
weapon=ue.MathLibrary.compose_transforms(frame,parent)
weapon.set_editor_property('scale3d',frame.scale3d)
root_bone=ue.AnimPoseExtensions.get_bone_pose(pose,'root',ue.AnimPoseSpaces.WORLD)
relative=ue.MathLibrary.make_relative_transform(weapon,root_bone)
if not ue.ERCharacter.configure_knife_grip_editor(preview,manual,data.mesh,relative):raise RuntimeError('Cannot configure reload-start editor')
for a in (preview,manual):
    if not assets.save_loaded_asset(a,False):raise RuntimeError('Cannot save editing preview')
for path,digest in protected.items():
    if hashlib.sha256(path.read_bytes()).hexdigest()!=digest:raise RuntimeError('Runtime asset changed: '+str(path))
ue.get_editor_subsystem(ue.AssetEditorSubsystem).open_editor_for_assets([manual])
ue.log('ER_PISTOL_RELOAD_START_EDITOR_READY '+manual.get_path_name())
