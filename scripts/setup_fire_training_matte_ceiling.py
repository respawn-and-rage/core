"""Bind opaque concrete slabs from the shared catalog to the Fire Training roof."""
from pathlib import Path
import hashlib
import unreal as ue

root = Path(__file__).resolve().parents[1]
assets = ue.EditorAssetLibrary
art = '/Game/EclipseRecoil/Art/'
theme = assets.load_asset(art+'MapTemplates/DA_Theme_FireTraining')
catalog = theme.get_editor_property('shared_surfaces')
ceiling, = [item for item in catalog.get_editor_property('surfaces') if item.get_name() == 'MI_ConcreteSlabs01']
palettes = list(theme.get_editor_property('themes'))
if any(palette.get_editor_property('ceiling') != ceiling for palette in palettes):
    content = root/'Assets/maps/Content'
    target = content/'EclipseRecoil/Art/MapTemplates/DA_Theme_FireTraining.uasset'
    def digest(path):
        return hashlib.sha256(path.read_bytes()).digest()
    protected = {p: digest(p) for p in content.rglob('*')
                 if p != target and p.suffix in {'.uasset', '.umap'} and p.is_file()}
    backup = root/'Saved/Testing/FireMatteCeiling/before.uasset'
    backup.parent.mkdir(parents=True, exist_ok=True)
    if not backup.exists():
        backup.write_bytes(target.read_bytes())
    if digest(backup) != digest(target):
        raise RuntimeError('Ceiling rollback differs from current package')
    for palette in palettes:
        palette.set_editor_property('ceiling', ceiling)
    theme.set_editor_property('themes', palettes)
    theme.set_editor_property('revision', theme.get_editor_property('revision')+1)
    recipe = assets.load_asset(art+'MapTemplates/DA_FireTraining')
    if not recipe.validate_layout(42) or not assets.save_loaded_asset(theme, False):
        raise RuntimeError('Ceiling validation or save failed')
    if any(digest(p) != expected for p, expected in protected.items()):
        raise RuntimeError('Protected package changed')
print('ER_FIRE_MATTE_CEILING_PASS')
