"""Import the prepared Skull Hunt art and wire the match Blueprint defaults.

Set ER_SKULL_IMPORT to the output directory from bake_skull_model.py.
Run in Unreal with -run=pythonscript -script=<absolute script path>.
"""
from datetime import datetime
from hashlib import sha256
import os
from pathlib import Path
from zipfile import ZipFile, ZIP_DEFLATED

import unreal as ue

root = Path(ue.Paths.project_dir())
source = Path(os.environ['ER_SKULL_IMPORT'])
folder = '/Game/EclipseRecoil/Art/SkullHunt'
mode_path = '/Game/EclipseRecoil/Core/BP_ERGameMode'
assets = ue.EditorAssetLibrary
tools = ue.AssetToolsHelpers.get_asset_tools()
edit = ue.MaterialEditingLibrary
owned = ('SM_Skull', 'T_Skull', 'M_Skull', 'BP_Skull')

def filename(package):
    return root / 'Content' / (package.removeprefix('/Game/') + '.uasset')

mode_file = filename(mode_path)
art_dir = filename(folder).with_suffix('')
protected = {path: sha256(path.read_bytes()).digest()
             for path in mode_file.parent.glob('*.uasset') if path != mode_file}
backup = root / 'Saved/AssetBackups' / ('SkullHunt-' + datetime.now().strftime('%Y%m%d-%H%M%S-%f') + '.zip')
backup.parent.mkdir(parents=True, exist_ok=True)
with ZipFile(backup, 'w', ZIP_DEFLATED) as archive:
    for path in [mode_file, *(art_dir / (name + '.uasset') for name in owned)]:
        if path.exists():
            archive.write(path, path.relative_to(root))

def save(asset):
    assert assets.save_loaded_asset(asset, only_if_is_dirty=True), asset.get_path_name()

def get_or_create(name, asset_type, factory):
    path = folder + '/' + name
    return assets.load_asset(path) if assets.does_asset_exist(path) else tools.create_asset(name, folder, asset_type, factory)

def import_asset(name, extension, options=None):
    path = folder + '/' + name
    if assets.does_asset_exist(path):
        return assets.load_asset(path)
    task = ue.AssetImportTask()
    task.set_editor_property('filename', str(source / (name + extension)))
    task.set_editor_property('destination_path', folder)
    task.set_editor_property('destination_name', name)
    task.set_editor_property('automated', True)
    if options is not None:
        task.set_editor_property('options', options)
    tools.import_asset_tasks([task])
    asset = assets.load_asset(path)
    assert asset, path
    return asset

assets.make_directory(folder)
options = ue.FbxImportUI()
for prop, value in dict(automated_import_should_detect_type=False, import_as_skeletal=False,
                        mesh_type_to_import=ue.FBXImportType.FBXIT_STATIC_MESH,
                        import_mesh=True, import_materials=False, import_textures=False).items():
    options.set_editor_property(prop, value)
data = options.get_editor_property('static_mesh_import_data')
data.set_editor_property('combine_meshes', True)
data.set_editor_property('auto_generate_collision', False)
mesh = import_asset('SM_Skull', '.fbx', options)
texture = import_asset('T_Skull', '.png')
bounds = mesh.get_bounds()
assert abs(bounds.box_extent.z * 2 - 24) < .1 and bounds.origin.length() < .1, bounds
assert min(bounds.box_extent.x, bounds.box_extent.y) < 3, bounds

material = get_or_create('M_Skull', ue.Material, ue.MaterialFactoryNew())
if not edit.get_num_material_expressions(material):
    material.set_editor_property('shading_model', ue.MaterialShadingModel.MSM_UNLIT)
    sample = edit.create_material_expression(material, ue.MaterialExpressionTextureSample, -620, 0)
    sample.set_editor_property('texture', texture)
    luminance = edit.create_material_expression(material, ue.MaterialExpressionDesaturation, -390, 0)
    tint = edit.create_material_expression(material, ue.MaterialExpressionVectorParameter, -390, 200)
    tint.set_editor_property('parameter_name', 'TeamColor')
    tint.set_editor_property('default_value', ue.LinearColor(1, 1, 1, 1))
    shaded = edit.create_material_expression(material, ue.MaterialExpressionMultiply, -160, 0)
    assert edit.connect_material_expressions(sample, '', luminance, '')
    assert edit.connect_material_expressions(luminance, '', shaded, 'A')
    assert edit.connect_material_expressions(tint, 'RGB', shaded, 'B')
    assert edit.connect_material_property(shaded, '', ue.MaterialProperty.MP_EMISSIVE_COLOR)
    edit.recompile_material(material)
if mesh.get_material(0) != material:
    mesh.set_material(0, material)
for asset in (texture, material, mesh):
    save(asset)

factory = ue.BlueprintFactory()
factory.set_editor_property('parent_class', ue.ERSkull)
skull = get_or_create('BP_Skull', ue.Blueprint, factory)
coin = ue.get_default_object(skull.generated_class()).get_editor_property('coin')
if coin.get_editor_property('static_mesh') != mesh:
    coin.set_editor_property('static_mesh', mesh)
    ue.BlueprintEditorLibrary.compile_blueprint(skull)
save(skull)

mode = assets.load_asset(mode_path)
assert mode, mode_path
defaults = ue.get_default_object(mode.generated_class())
if defaults.get_editor_property('skull_class') != skull.generated_class():
    defaults.set_editor_property('skull_class', skull.generated_class())
    ue.BlueprintEditorLibrary.compile_blueprint(mode)
save(mode)

assert ue.get_default_object(mode.generated_class()).get_editor_property('skull_class') == skull.generated_class()
assert ue.get_default_object(skull.generated_class()).get_editor_property('coin').get_editor_property('static_mesh') == mesh
assert all(sha256(path.read_bytes()).digest() == digest for path, digest in protected.items())
ue.log(f'ER_SKULL_ASSETS_PASS bounds={bounds} backup={backup}')
