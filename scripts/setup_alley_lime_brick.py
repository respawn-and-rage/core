"""Bind the shared lime-coloured brick to Middle Eastern Alleys in Unreal."""
import hashlib
from pathlib import Path

import unreal as ue

root = Path(__file__).resolve().parents[1]
art = '/Game/EclipseRecoil/Art/MapTemplates/'
assets = ue.EditorAssetLibrary
theme = assets.load_asset(art + 'DA_Theme_MiddleEasternAlleys')
catalog = theme.get_editor_property('shared_surfaces')
brick = next(item for item in catalog.get_editor_property('surfaces')
             if item.get_name() == 'MI_BrickPainted01')
palettes = list(theme.get_editor_property('themes'))


def digest(path):
    with path.open('rb') as stream:
        return hashlib.file_digest(stream, 'sha256').hexdigest()


if any(palette.get_editor_property('materials')[1] != brick for palette in palettes):
    content = root / 'Assets/maps/Content'
    target = content / 'EclipseRecoil/Art/MapTemplates/DA_Theme_MiddleEasternAlleys.uasset'
    protected = {path: digest(path) for path in content.rglob('*')
                 if path != target and path.suffix in {'.uasset', '.umap'} and path.is_file()}
    backup = root / 'Saved/Testing/AlleyLimeBrick/before.uasset'
    backup.parent.mkdir(parents=True, exist_ok=True)
    if not backup.exists():
        backup.write_bytes(target.read_bytes())
    if digest(backup) != digest(target):
        raise RuntimeError('Alleys theme backup differs from the current package')

    for palette in palettes:
        finishes = list(palette.get_editor_property('materials'))
        finishes[1] = brick
        palette.set_editor_property('materials', finishes)
    theme.set_editor_property('themes', palettes)
    theme.set_editor_property('revision', theme.get_editor_property('revision') + 1)
    recipe = assets.load_asset(art + 'DA_MiddleEasternAlleys')
    if not all(recipe.validate_layout(seed) for seed in (42, 73, 7919)):
        raise RuntimeError('Alleys layout validation failed')
    if not assets.save_loaded_asset(theme, False):
        raise RuntimeError('Alleys theme save failed')
    if any(digest(path) != expected for path, expected in protected.items()):
        raise RuntimeError('Protected package changed')

print('ER_ALLEY_LIME_BRICK_PASS')
