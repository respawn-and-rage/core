"""Replace legacy lamp finishes and detach Village's exterior trees via Unreal."""
from pathlib import Path
import json
import hashlib
import unreal as ue

root = Path(__file__).resolve().parents[1]
a = ue.EditorAssetLibrary
e = ue.MaterialEditingLibrary
base = '/Game/EclipseRecoil/Art/MapTemplates/'
out = root/'Saved/Testing/CeilingPanels'
out.mkdir(parents=True, exist_ok=True)
changed = {base+'DA_Rooms', base+'DA_Village'}
protected = {}
for file in (root/'Assets/maps/Content').rglob('*'):
    if file.is_file() and file.suffix in {'.uasset', '.umap'}:
        package = '/Game/'+file.relative_to(root/'Assets/maps/Content').with_suffix('').as_posix()
        if package not in changed and '/Art/Lighting/' not in package:
            protected[file] = hashlib.sha256(file.read_bytes()).hexdigest()
path = '/Game/EclipseRecoil/Art/Lighting/M_OfficeCeilingWhite'
if not a.does_asset_exist(path):
    material = ue.AssetToolsHelpers.get_asset_tools().create_asset('M_OfficeCeilingWhite', '/Game/EclipseRecoil/Art/Lighting', ue.Material, ue.MaterialFactoryNew())
    material.set_editor_property('shading_model', ue.MaterialShadingModel.MSM_UNLIT)
    material.set_editor_property('used_with_instanced_static_meshes', True)
    color = e.create_material_expression(material, ue.MaterialExpressionConstant3Vector, -400, 0)
    color.set_editor_property('constant', ue.LinearColor(4, 4, 4, 1))
    e.connect_material_property(color, '', ue.MaterialProperty.MP_EMISSIVE_COLOR)
    e.recompile_material(material)
    a.save_loaded_asset(material)
material = a.load_asset(path)
for name in ['Rooms', 'Village']:
    recipe = a.load_asset(base+'DA_'+name)
    marker = 'EROfficePanelsNoTrees'
    if a.get_metadata_tag(recipe, marker) == '1':
        continue
    file = root/('Content/'+base.removeprefix('/Game/')+'DA_'+name+'.uasset')
    backup = out/(name+'-before.uasset')
    if not backup.exists():
        backup.write_bytes(file.read_bytes())
        if hashlib.sha256(backup.read_bytes()).digest() != hashlib.sha256(file.read_bytes()).digest():
            raise RuntimeError('Invalid rollback '+name)
    if name == 'Rooms':
        settings = recipe.get_editor_property('rooms')
        settings.set_editor_property('lamp_surface', material)
        recipe.set_editor_property('rooms', settings)
    else:
        settings = recipe.get_editor_property('village')
        (out/'removed-trees.json').write_text(json.dumps([m.get_path_name() for m in settings.get_editor_property('exterior_trees')], indent=2))
        settings.set_editor_property('exterior_trees', [])
        settings.set_editor_property('foliage_surface', None)
        settings.set_editor_property('trunk_surface', None)
        recipe.set_editor_property('village', settings)
    recipe.set_editor_property('revision', recipe.get_editor_property('revision')+1)
    a.set_metadata_tag(recipe, marker, '1')
    if not recipe.validate_layout(42):
        raise RuntimeError('Invalid recipe '+name)
    a.save_loaded_asset(recipe)
for file, digest in protected.items():
    if hashlib.sha256(file.read_bytes()).hexdigest() != digest:
        raise RuntimeError('Protected asset changed: '+str(file))
print('ER_OFFICE_PANELS_NO_TREES_PASS')
