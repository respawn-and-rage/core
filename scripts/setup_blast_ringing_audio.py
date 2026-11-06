"""Import the local Freesound 377205 WAV through Unreal's Python commandlet.

Set ER_BLAST_RINGING_WAV to the original file. Only the three BlastRinging
packages are saved. Existing packages are backed up before import.
"""
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import wave

import unreal as ue

folder = '/Game/EclipseRecoil/Audio/BlastRinging'
names = ('SM_ER_Master', 'SM_ER_World', 'S_ER_BlastRinging')
source = Path(os.environ['ER_BLAST_RINGING_WAV'])
backup = Path(tempfile.mkdtemp(prefix='er-blast-ringing-'))
content = Path(ue.Paths.convert_relative_path_to_full(ue.Paths.project_content_dir()))
for name in names:
    existing = content / 'EclipseRecoil/Audio/BlastRinging' / (name + '.uasset')
    if existing.exists():
        shutil.copy2(existing, backup / existing.name)
ue.log(f'ER_BLAST_RINGING_BACKUP {backup}')

tools = ue.AssetToolsHelpers.get_asset_tools()
def get_submix(name):
    path = f'{folder}/{name}'
    if ue.EditorAssetLibrary.does_asset_exist(path):
        return ue.EditorAssetLibrary.load_asset(path)
    return tools.create_asset(name, folder, ue.SoundSubmix, ue.SoundSubmixFactory())


master, world = (get_submix(name) for name in names[:2])
with tempfile.TemporaryDirectory() as scratch:
    wav_path = str(Path(scratch) / (names[2] + '.wav'))
    subprocess.run(['/usr/bin/afconvert', '-f', 'WAVE', '-d', 'LEI16',
                    str(source), wav_path], check=True)
    with wave.open(wav_path, 'rb') as derived:
        assert (derived.getnchannels(), derived.getsampwidth(), derived.getframerate()) == (1, 2, 48000)
        assert derived.getnframes() == 20 * 48000
    task = ue.AssetImportTask()
    for key, value in dict(filename=wav_path, destination_path=folder,
                           destination_name=names[2], automated=True,
                           replace_existing=True, save=False).items():
        task.set_editor_property(key, value)
    tools.import_asset_tasks([task])
    sound, = task.get_objects()

sound.set_editor_property('sound_submix_object', master)
sound.set_editor_property('looping', False)
sound.set_editor_property('virtualization_mode', ue.VirtualizationMode.PLAY_WHEN_SILENT)
sound.set_editor_property('loading_behavior', ue.SoundWaveLoadingBehavior.FORCE_INLINE)
sound.set_sound_asset_compression_type(ue.SoundAssetCompressionType.PCM)
assert sound.get_path_name() == f'{folder}/{names[2]}.{names[2]}'
assert sound.get_editor_property('num_channels') == 1
assert sound.get_editor_property('sound_submix_object') == master
for asset in (master, world, sound):
    assert ue.EditorAssetLibrary.save_loaded_asset(asset, only_if_is_dirty=False)
ue.log('ER_BLAST_RINGING_READY')
