"""Import owned water takes from the maps asset source directory."""
from pathlib import Path
import unreal as ue

project = Path(ue.Paths.convert_relative_path_to_full(ue.Paths.project_dir()))
source = project / 'Assets/weapons/SourceAssets/Freesound/Water'
folder = '/Game/EclipseRecoil/Audio/Water'
assets = ue.EditorAssetLibrary
tools = ue.AssetToolsHelpers.get_asset_tools()
for name in ('S_ER_Wade_A', 'S_ER_Wade_B', 'S_ER_WaterExplosion'):
    task = ue.AssetImportTask()
    for key, value in dict(filename=str(source / (name + '.wav')),
                           destination_path=folder, destination_name=name,
                           automated=True, replace_existing=True, save=False).items():
        task.set_editor_property(key, value)
    tools.import_asset_tasks([task])
    assert task.get_editor_property('imported_object_paths'), name
    sound = ue.load_asset(f'{folder}/{name}')
    sound.set_editor_property('looping', False)
    assert assets.save_loaded_asset(sound, False)
ue.log('ER_WATER_AUDIO_READY')
