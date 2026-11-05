"""Import the two mono glass break derivatives and their spatial attenuation."""
import hashlib
from pathlib import Path
import shutil
import tempfile

import unreal as ue

project = Path(ue.Paths.convert_relative_path_to_full(ue.Paths.project_dir()))
source = project / 'Assets/weapons/SourceAssets/Freesound/GlassBreak'
folder = '/Game/EclipseRecoil/Audio/GlassBreak'
tools = ue.AssetToolsHelpers.get_asset_tools()
assets = ue.EditorAssetLibrary
credits = (source / 'CREDIT.txt').read_text().strip().split('\n\n')
names = ['S_ER_GlassBreak_221528', 'S_ER_GlassBreak_41348', 'SA_ER_GlassBreak']
packages = [
    project / 'Content/EclipseRecoil/Audio/GlassBreak' / (name + '.uasset') for name in names
]
existing = [package for package in packages if package.exists()]
if existing:
    backup = tempfile.mkdtemp(prefix='er-glass-audio-backup-')
    for package in existing:
        shutil.copy2(package, backup)
    ue.log(f'Glass break audio backup: {backup}')

for name, credit in zip(names[:2], credits, strict=True):
    wav = source / (name.removeprefix('S_') + '.wav')
    task = ue.AssetImportTask()
    for key, value in dict(filename=str(wav), destination_path=folder,
                           destination_name=name, automated=True,
                           replace_existing=True, save=False).items():
        task.set_editor_property(key, value)
    tools.import_asset_tasks([task])
    assert task.get_editor_property('imported_object_paths'), f'Import failed for {wav}'
    sound = ue.load_asset(f'{folder}/{name}')
    assert sound.get_editor_property('num_channels') == 1
    sound.set_editor_property('looping', False)
    sound.set_sound_asset_compression_type(ue.SoundAssetCompressionType.PCM)
    sound.set_editor_property('loading_behavior', ue.SoundWaveLoadingBehavior.FORCE_INLINE)
    assets.set_metadata_tag(sound, 'Attribution', credit)
    assets.set_metadata_tag(sound, 'ERAudioSourceSHA256', hashlib.sha256(wav.read_bytes()).hexdigest())
    assert assets.save_loaded_asset(sound, False)

attenuation = ue.load_asset(f'{folder}/{names[2]}') or tools.create_asset(
    names[2], folder, ue.SoundAttenuation, ue.SoundAttenuationFactory())
settings = attenuation.get_editor_property('attenuation')
settings.set_editor_property('attenuate', True)
settings.set_editor_property('spatialize', True)
settings.set_editor_property('attenuation_shape_extents', ue.Vector(300, 0, 0))
settings.set_editor_property('falloff_distance', 3700.)
attenuation.set_editor_property('attenuation', settings)
assert assets.save_loaded_asset(attenuation, False)
ue.log('ER_GLASS_BREAK_AUDIO_READY variants=2')
