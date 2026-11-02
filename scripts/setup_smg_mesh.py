"""Reimport only the owned SMG mesh after baking; preserve clips and gameplay data."""
from pathlib import Path
import hashlib
import zipfile
import unreal as ue

project = Path(ue.Paths.project_dir()).resolve()
weapon_root = project/'Assets/weapons'
packages = weapon_root/'Content'
mesh_path = '/Game/EclipseRecoil/Weapons/Sketchfab/SMG/V4/SK_SMG'
mesh = ue.load_asset(mesh_path)
assert mesh, mesh_path
owned = packages/'EclipseRecoil/Weapons/Sketchfab/SMG/V4/SK_SMG.uasset'
protected = {p: hashlib.sha256(p.read_bytes()).hexdigest()
             for p in packages.rglob('*.uasset') if p != owned}
backup = project/'Saved/Backups/SMGMesh.zip'
backup.parent.mkdir(parents=True, exist_ok=True)
if not backup.exists():
    with zipfile.ZipFile(backup, 'x', zipfile.ZIP_DEFLATED) as archive:
        archive.write(owned, owned.name)
materials = mesh.get_editor_property('materials')
options = ue.FbxImportUI()
for prop, value in dict(automated_import_should_detect_type=False,
        mesh_type_to_import=ue.FBXImportType.FBXIT_SKELETAL_MESH,
        import_as_skeletal=True, import_mesh=True, import_animations=False,
        import_materials=False, import_textures=False, create_physics_asset=False,
        skeleton=mesh.get_editor_property('skeleton')).items():
    options.set_editor_property(prop, value)
options.skeletal_mesh_import_data.set_editor_property('update_skeleton_reference_pose', False)
source = next((weapon_root/'SourceAssets/SketchfabWeapons').glob('ca37*/derived/SK_SMG.fbx'))
task = ue.AssetImportTask()
for prop, value in dict(filename=str(source), destination_path=mesh_path.rsplit('/', 1)[0],
        destination_name='SK_SMG', automated=True, save=False, replace_existing=True,
        options=options).items():
    task.set_editor_property(prop, value)
ue.AssetToolsHelpers.get_asset_tools().import_asset_tasks([task])
assert task.get_editor_property('imported_object_paths'), 'SMG import failed'
mesh = ue.load_asset(mesh_path)
mesh.set_editor_property('materials', materials)
assert ue.EditorAssetLibrary.save_loaded_asset(mesh, only_if_is_dirty=False)
assert all(hashlib.sha256(p.read_bytes()).hexdigest() == digest
           for p, digest in protected.items()), 'Protected weapon package changed'
ue.log('ER_SMG_MESH_COMPLETE preserved_clips_and_data=True')
