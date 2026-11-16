"""Import Collect_Point_00 and assign it to BP_Skull through Unreal Python.

Set ER_SKULL_PICKUP_WAV to the original Freesound 270304 WAV on macOS.
"""
from hashlib import sha256
import os
from pathlib import Path
import shutil
import subprocess
import tempfile

import unreal as ue

art = 'EclipseRecoil/Art/SkullHunt'
audio = 'EclipseRecoil/Audio/SkullHunt'
name = 'S_ER_SkullPickup'
content = Path(ue.Paths.convert_relative_path_to_full(ue.Paths.project_content_dir()))
blueprint_file = content / art / 'BP_Skull.uasset'
sound_file = content / audio / (name + '.uasset')
protected = {path: sha256(path.read_bytes()).digest()
             for folder in (art, audio) for path in (content / folder).glob('*.uasset')
             if path not in (blueprint_file, sound_file)}

assets = ue.EditorAssetLibrary
blueprint = assets.load_asset(f'/Game/{art}/BP_Skull')
assert blueprint, blueprint_file
defaults = ue.get_default_object(blueprint.generated_class())
assert defaults.get_editor_property('coin').get_editor_property('relative_scale3d') == ue.Vector(3, 3, 3)

backup_root = Path(ue.Paths.convert_relative_path_to_full(ue.Paths.project_saved_dir())) / 'AssetBackups'
backup_root.mkdir(parents=True, exist_ok=True)
backup = Path(tempfile.mkdtemp(prefix='SkullPickup-', dir=backup_root))
for path in (blueprint_file, sound_file):
    if path.exists():
        shutil.copy2(path, backup / path.name)
ue.log(f'ER_SKULL_PICKUP_BACKUP {backup}')

package = f'/Game/{audio}/{name}'
sound = assets.load_asset(package) if assets.does_asset_exist(package) else None
if sound is None:
    with tempfile.TemporaryDirectory() as scratch:
        wav = Path(scratch) / (name + '.wav')
        subprocess.run(['/usr/bin/afconvert', '-f', 'WAVE', '-d', 'LEI16',
                        os.environ['ER_SKULL_PICKUP_WAV'], str(wav)], check=True)
        task = ue.AssetImportTask()
        for key, value in dict(filename=str(wav), destination_path=f'/Game/{audio}',
                               destination_name=name, automated=True, save=False).items():
            task.set_editor_property(key, value)
        ue.AssetToolsHelpers.get_asset_tools().import_asset_tasks([task])
        sound, = task.get_objects()
    assert sound.get_path_name() == f'{package}.{name}'
    sound.set_editor_property('loading_behavior', ue.SoundWaveLoadingBehavior.FORCE_INLINE)
    assert sound.get_editor_property('num_channels') == 1
    assert abs(sound.get_editor_property('duration') - .625) < .01
    assert assets.save_loaded_asset(sound)

if defaults.get_editor_property('pickup_sound') != sound:
    defaults.set_editor_property('pickup_sound', sound)
    ue.BlueprintEditorLibrary.compile_blueprint(blueprint)
    assert assets.save_loaded_asset(blueprint)
assert ue.get_default_object(blueprint.generated_class()).get_editor_property('pickup_sound') == sound
assert all(sha256(path.read_bytes()).digest() == digest for path, digest in protected.items())
ue.log('ER_SKULL_PICKUP_READY')
