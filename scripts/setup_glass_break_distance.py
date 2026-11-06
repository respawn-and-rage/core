"""Tighten the owned glass-break attenuation without reimporting the wave assets."""
import hashlib
from pathlib import Path
import shutil

import unreal as ue

project = Path(ue.Paths.convert_relative_path_to_full(ue.Paths.project_dir()))
folder = project / 'Content/EclipseRecoil/Audio/GlassBreak'
attenuation_path = folder / 'SA_ER_GlassBreak.uasset'
wave_paths = [folder / f'{name}.uasset' for name in
              ('S_ER_GlassBreak_221528', 'S_ER_GlassBreak_41348')]
assert attenuation_path.is_file() and all(path.is_file() for path in wave_paths)
wave_hashes = {path: hashlib.sha256(path.read_bytes()).hexdigest() for path in wave_paths}

asset = ue.load_asset('/Game/EclipseRecoil/Audio/GlassBreak/SA_ER_GlassBreak')
assert asset is not None
settings = asset.get_editor_property('attenuation')
extent = settings.get_editor_property('attenuation_shape_extents')
already_set = (settings.get_editor_property('attenuate') and
               settings.get_editor_property('spatialize') and
               abs(extent.x - 125.) < .01 and
               abs(settings.get_editor_property('falloff_distance') - 2200.) < .01)
if not already_set:
    backup = project / 'Saved/Testing/Backups/GlassAttenuation'
    backup.mkdir(parents=True, exist_ok=True)
    original_hash = hashlib.sha256(attenuation_path.read_bytes()).hexdigest()
    saved_copy = backup / f'SA_ER_GlassBreak-{original_hash[:12]}.uasset'
    if not saved_copy.exists():
        shutil.copy2(attenuation_path, saved_copy)
    settings.set_editor_property('attenuate', True)
    settings.set_editor_property('spatialize', True)
    settings.set_editor_property('attenuation_shape_extents', ue.Vector(125, 0, 0))
    settings.set_editor_property('falloff_distance', 2200.)
    asset.set_editor_property('attenuation', settings)
    assert ue.EditorAssetLibrary.save_loaded_asset(asset, False)
    ue.log(f'ER_GLASS_DISTANCE_UPDATED backup={saved_copy}')
else:
    ue.log('ER_GLASS_DISTANCE_CURRENT')

assert all(hashlib.sha256(path.read_bytes()).hexdigest() == digest
           for path, digest in wave_hashes.items())
