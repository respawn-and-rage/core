"""Bind coarser roof paving and brick to the Middle Eastern Alleys palette."""
from pathlib import Path
import hashlib
import unreal as ue

root = Path(__file__).resolve().parents[1]
assets = ue.EditorAssetLibrary
art = '/Game/EclipseRecoil/Art/'
theme = assets.load_asset(art+'MapTemplates/DA_Theme_MiddleEasternAlleys')
catalog = theme.get_editor_property('shared_surfaces')
by_name = {item.get_name(): item for item in catalog.get_editor_property('surfaces')}
paving = by_name['MI_PavementSquare01']
brick = by_name['MI_BrickPainted01']
palettes = list(theme.get_editor_property('themes'))
if any(p.get_editor_property('floor') != paving or p.get_editor_property('ceiling') != paving
       or p.get_editor_property('materials')[1] != brick for p in palettes):
    content = root/'Assets/maps/Content'
    target = content/'EclipseRecoil/Art/MapTemplates/DA_Theme_MiddleEasternAlleys.uasset'
    def digest(path):
        return hashlib.sha256(path.read_bytes()).digest()
    protected = {p: digest(p) for p in content.rglob('*')
                 if p != target and p.suffix in {'.uasset', '.umap'} and p.is_file()}
    backup = root/'Saved/Testing/AlleySurfaceReadability/before.uasset'
    backup.parent.mkdir(parents=True, exist_ok=True)
    if not backup.exists():
        backup.write_bytes(target.read_bytes())
    if digest(backup) != digest(target):
        raise RuntimeError('Ceiling rollback differs from current package')
    for palette in palettes:
        palette.set_editor_property('floor', paving)
        palette.set_editor_property('ceiling', paving)
        finishes = list(palette.get_editor_property('materials'))
        finishes[1] = brick
        palette.set_editor_property('materials', finishes)
    theme.set_editor_property('themes', palettes)
    theme.set_editor_property('revision', theme.get_editor_property('revision')+1)
    recipe = assets.load_asset(art+'MapTemplates/DA_MiddleEasternAlleys')
    if not recipe.validate_layout(42) or not assets.save_loaded_asset(theme, False):
        raise RuntimeError('Ceiling validation or save failed')
    if any(digest(p) != expected for p, expected in protected.items()):
        raise RuntimeError('Protected package changed')
print('ER_ALLEY_SURFACES_PASS')
