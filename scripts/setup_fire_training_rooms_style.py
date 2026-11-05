"""Use the Rooms corridor palette and lower ambient fill in Fire Training."""
from pathlib import Path
import hashlib
import unreal as ue

root = Path(__file__).resolve().parents[1]
assets = ue.EditorAssetLibrary
art = '/Game/EclipseRecoil/Art/'
theme = assets.load_asset(art+'MapTemplates/DA_Theme_FireTraining')
rooms = assets.load_asset(art+'MapTemplates/DA_Theme_Rooms')
reference = rooms.get_editor_property('themes')[0]
finishes = {role: reference.get_editor_property(role) for role in ('wall', 'floor', 'ceiling')}
palettes = list(theme.get_editor_property('themes'))
content = root/'Assets/maps/Content'
target = content/'EclipseRecoil/Art/MapTemplates/DA_Theme_FireTraining.uasset'
level_file = content/'EclipseRecoil/Maps/L_FireTraining.umap'

def digest(path):
    return hashlib.sha256(path.read_bytes()).digest()

protected = {p: digest(p) for p in content.rglob('*')
             if p not in {target, level_file} and p.suffix in {'.uasset', '.umap'} and p.is_file()}
if any(any(palette.get_editor_property(role) != value for role, value in finishes.items())
       or list(palette.get_editor_property('grounds')) != [finishes['floor']] for palette in palettes):
    backup = root/'Saved/Testing/FireRoomsStyle/before-theme.uasset'
    backup.parent.mkdir(parents=True, exist_ok=True)
    if not backup.exists():
        backup.write_bytes(target.read_bytes())
    if digest(backup) != digest(target):
        raise RuntimeError('Ceiling rollback differs from current package')
    for palette in palettes:
        for role, value in finishes.items():
            palette.set_editor_property(role, value)
        palette.set_editor_property('grounds', [finishes['floor']])
    theme.set_editor_property('themes', palettes)
    theme.set_editor_property('revision', theme.get_editor_property('revision')+1)
    recipe = assets.load_asset(art+'MapTemplates/DA_FireTraining')
    if not recipe.validate_layout(42) or not assets.save_loaded_asset(theme, False):
        raise RuntimeError('Ceiling validation or save failed')
level = '/Game/EclipseRecoil/Maps/L_FireTraining'
if not ue.EditorLoadingAndSavingUtils.load_map(level):
    raise RuntimeError('Cannot load Fire Training')
world = ue.get_editor_subsystem(ue.UnrealEditorSubsystem).get_editor_world()
sky, = ue.GameplayStatics.get_all_actors_of_class(world, ue.SkyLight)
light = sky.light_component
if abs(light.get_editor_property('intensity') - .12) > .0001:
    file = level_file
    backup = root/'Saved/Testing/FireRoomsStyle/before-lighting.umap'
    backup.parent.mkdir(parents=True, exist_ok=True)
    if not backup.exists():
        backup.write_bytes(file.read_bytes())
    if backup.read_bytes() != file.read_bytes():
        raise RuntimeError('Lighting rollback differs from current level')
    light.set_editor_property('intensity', .12)
    if not ue.EditorLoadingAndSavingUtils.save_current_level():
        raise RuntimeError('Cannot save Fire Training ambient lighting')
if any(digest(p) != expected for p, expected in protected.items()):
    raise RuntimeError('Protected package changed')
print('ER_FIRE_ROOMS_STYLE_PASS')
