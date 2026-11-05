"""Bind owned white office ceiling panels to Fire Training through Unreal."""
from pathlib import Path
import hashlib
import json
import unreal as ue

root = Path(__file__).resolve().parents[1]
a = ue.EditorAssetLibrary
out = root/'Saved/Testing/FireCeilingPanels'
out.mkdir(parents=True, exist_ok=True)
target = '/Game/EclipseRecoil/Art/MapTemplates/DA_FireTraining'
owned = '/Game/EclipseRecoil/Art/Lighting/SM_OfficeCeilingPanel'
protected = {}
for file in (root/'Assets/maps/Content').rglob('*'):
    if file.is_file() and file.suffix in {'.uasset', '.umap'}:
        package = '/Game/'+file.relative_to(root/'Assets/maps/Content').with_suffix('').as_posix()
        if package not in {target, owned}:
            protected[file] = hashlib.sha256(file.read_bytes()).hexdigest()
recipe = a.load_asset(target)
if a.get_metadata_tag(recipe, 'EROfficeCeilingPanels') != '1':
    file = root/('Content/'+target.removeprefix('/Game/')+'.uasset')
    backup = out/'before.uasset'
    if not backup.exists():
        backup.write_bytes(file.read_bytes())
        assert hashlib.sha256(backup.read_bytes()).digest() == hashlib.sha256(file.read_bytes()).digest()
    mesh = a.load_asset(owned) if a.does_asset_exist(owned) else a.duplicate_asset('/Engine/BasicShapes/Cube', owned)
    material = a.load_asset('/Game/EclipseRecoil/Art/Lighting/M_OfficeCeilingWhite')
    if not mesh or not material:
        raise RuntimeError('Missing owned office lighting')
    mesh.set_material(0, material)
    a.save_loaded_asset(mesh)
    settings = recipe.get_editor_property('fire_training')
    settings.set_editor_property('ceiling_panel_mesh', mesh)
    recipe.set_editor_property('fire_training', settings)
    recipe.set_editor_property('revision', recipe.get_editor_property('revision')+1)
    if not recipe.validate_layout(42):
        raise RuntimeError('Fire Training layout validation failed')
    a.set_metadata_tag(recipe, 'EROfficeCeilingPanels', '1')
    a.save_loaded_asset(recipe)
bound = recipe.get_editor_property('fire_training').get_editor_property('ceiling_panel_mesh')
if not bound or bound.get_path_name().split('.')[0] != owned or bound.get_material(0) != a.load_asset('/Game/EclipseRecoil/Art/Lighting/M_OfficeCeilingWhite'):
    raise RuntimeError('Office panel binding changed')
for file, digest in protected.items():
    if hashlib.sha256(file.read_bytes()).hexdigest() != digest:
        raise RuntimeError('Protected asset changed: '+str(file))
(out/'receipt.json').write_text(json.dumps({'result':'PASS','protected_packages':len(protected),'mesh':owned},indent=2)+'\n')
print('ER_FIRE_CEILING_PANELS_PASS')
