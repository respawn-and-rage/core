"""Bind finishes with visible large-scale patterns in Central Building, Town and Village."""
from pathlib import Path
import hashlib
import unreal as ue

root = Path(__file__).resolve().parents[1]
assets = ue.EditorAssetLibrary
art = '/Game/EclipseRecoil/Art/'
# Rooms and Fire Training already use the jointed corridor concrete.
# The stone and plank finishes already give the other palettes a clear pattern.
changes = {
    'CentralBuilding': {'brick': 'MI_BrickRough03', 'concrete': 'MI_PouredConcrete01',
                        'wall': 'MI_PouredConcrete01', 'ceiling': 'MI_ConcreteSlabs01'},
    'Village': {'metal': 'MI_SlateRoofing01', 'ceiling': 'MI_SlateRoofing01'},
    'Town': {'brick': 'MI_BrickRough03', 'concrete': 'MI_PouredConcrete01',
             'wall': 'MI_PouredConcrete01'},
}
content = root/'Assets/maps/Content'
targets = {content/f'EclipseRecoil/Art/MapTemplates/DA_Theme_{name}.uasset' for name in changes}
def digest(path):
    return hashlib.sha256(path.read_bytes()).digest()
protected = {p: digest(p) for p in content.rglob('*')
             if p not in targets and p.suffix in {'.uasset', '.umap'} and p.is_file()}
out = root/'Saved/Testing/MapSurfaceReadability'
out.mkdir(parents=True, exist_ok=True)
for name, roles in changes.items():
    theme = assets.load_asset(art+f'MapTemplates/DA_Theme_{name}')
    catalog = theme.get_editor_property('shared_surfaces')
    by_name = {item.get_name(): item for item in catalog.get_editor_property('surfaces')}
    palettes = list(theme.get_editor_property('themes'))
    changed = False
    for palette in palettes:
        finishes = list(palette.get_editor_property('materials'))
        for role, finish in roles.items():
            material = by_name[finish]
            if role in ('brick', 'concrete', 'metal'):
                index = {'brick': 1, 'concrete': 2, 'metal': 5}[role]
                if finishes[index] != material:
                    finishes[index] = material
                    changed = True
            elif palette.get_editor_property(role) != material:
                palette.set_editor_property(role, material)
                changed = True
        palette.set_editor_property('materials', finishes)
    if changed:
        file = content/f'EclipseRecoil/Art/MapTemplates/DA_Theme_{name}.uasset'
        backup = out/('before-matte-'+file.name)
        if not backup.exists():
            backup.write_bytes(file.read_bytes())
        if digest(backup) != digest(file):
            raise RuntimeError('Invalid rollback for '+name)
        theme.set_editor_property('themes', palettes)
        theme.set_editor_property('revision', theme.get_editor_property('revision')+1)
        recipe = assets.load_asset(art+f'MapTemplates/DA_{name}')
        if not recipe.validate_layout(42) or not assets.save_loaded_asset(theme, False):
            raise RuntimeError('Palette validation or save failed for '+name)
if any(digest(p) != expected for p, expected in protected.items()):
    raise RuntimeError('Protected package changed')
print('ER_READABLE_MAP_SURFACES_PASS')
