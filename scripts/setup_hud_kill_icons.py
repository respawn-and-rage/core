"""Import the supplied HUD badges and assign them in the match HUD Blueprint.

Run via -run=pythonscript -script=<absolute path>. ER_HUD_KILL_ICON_SOURCE
must point to a directory containing the original kill.png and headshot.png.
"""
from datetime import datetime
from hashlib import sha256
import os
from pathlib import Path
from zipfile import ZipFile, ZIP_DEFLATED

import unreal as ue

root = Path(ue.Paths.convert_relative_path_to_full(ue.Paths.project_dir()))
source = Path(os.environ['ER_HUD_KILL_ICON_SOURCE'])
folder = '/Game/EclipseRecoil/Art/HUDKillIcons'
output = root / 'Content' / 'EclipseRecoil' / 'Art' / 'HUDKillIcons'
owned = {'T_Skull.uasset', 'T_Headshot.uasset', 'BP_ERMatchHUD.uasset'}
assets = ue.EditorAssetLibrary
tools = ue.AssetToolsHelpers.get_asset_tools()

assert all((source / name).is_file() for name in ('kill.png', 'headshot.png'))
protected = {p: sha256(p.read_bytes()).digest() for p in output.glob('*.uasset') if p.name not in owned}
existing = [output / name for name in owned if (output / name).exists()]
if existing:
    backup = root / 'Saved' / 'AssetBackups' / ('HUDKillIcons-' + datetime.now().strftime('%Y%m%d-%H%M%S-%f') + '.zip')
    backup.parent.mkdir(parents=True, exist_ok=True)
    with ZipFile(backup, 'w', ZIP_DEFLATED) as archive:
        for path in existing:
            archive.write(path, path.relative_to(root))


def texture(name, filename):
    package = folder + '/' + name
    if not assets.does_asset_exist(package):
        task = ue.AssetImportTask()
        for key, value in dict(filename=str(source / filename), destination_path=folder,
                               destination_name=name, automated=True, save=False).items():
            task.set_editor_property(key, value)
        tools.import_asset_tasks([task])
    result = assets.load_asset(package)
    assert isinstance(result, ue.Texture2D), package
    for key, value in dict(lod_group=ue.TextureGroup.TEXTUREGROUP_UI,
                           compression_settings=ue.TextureCompressionSettings.TC_EDITOR_ICON,
                           mip_gen_settings=ue.TextureMipGenSettings.TMGS_SIMPLE_AVERAGE,
                           filter=ue.TextureFilter.TF_TRILINEAR, never_stream=True).items():
        if result.get_editor_property(key) != value:
            result.set_editor_property(key, value)
    assert assets.save_loaded_asset(result)
    return result


skull = texture('T_Skull', 'kill.png')
headshot = texture('T_Headshot', 'headshot.png')
name = 'BP_ERMatchHUD'
path = folder + '/' + name
if assets.does_asset_exist(path):
    blueprint = assets.load_asset(path)
else:
    factory = ue.BlueprintFactory()
    factory.set_editor_property('parent_class', ue.load_class(None, '/Script/fdm.ERHUD'))
    blueprint = tools.create_asset(name, folder, ue.Blueprint, factory)
assert blueprint
defaults = ue.get_default_object(blueprint.generated_class())
for key, value in dict(skull_badge=skull, headshot_badge=headshot).items():
    if defaults.get_editor_property(key) != value:
        defaults.set_editor_property(key, value)
assert assets.save_loaded_asset(blueprint)
assert all(sha256(p.read_bytes()).digest() == digest for p, digest in protected.items())
ue.log('ER_HUD_KILL_ICONS_IMPORT_PASS')
