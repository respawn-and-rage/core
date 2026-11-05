"""Replace the retired diamond plate in the installed shared material library."""
import json
from pathlib import Path
import runpy
import zipfile

import unreal as ue

root = Path(__file__).resolve().parents[1]
assets = ue.EditorAssetLibrary
base = '/Game/EclipseRecoil/Art/SharedMaterials'
metal = base + '/Source/Materials/Metal/MI_'
catalog_path = base + '/DA_SharedMapSurfaces'
retired_path = metal + 'DiamondPlate01'
panel_path = metal + 'AluPanels150x150'
templates = ['CentralBuilding', 'MiddleEasternAlleys', 'FireTraining', 'Rooms', 'Village', 'Town']
recipes = ['/Game/EclipseRecoil/Art/MapTemplates/DA_' + name for name in templates]
manifest = runpy.run_path(str(root / 'scripts/shared_material_catalog.py'))['CATALOG']
sources = sorted({path for family in manifest['families'].values() for path in family})


def load(path):
    asset = assets.load_asset(path)
    if not asset:
        raise RuntimeError('Missing ' + path)
    return asset


def save(asset):
    if not assets.save_loaded_asset(asset, False):
        raise RuntimeError('Could not save ' + asset.get_path_name())


def main():
    catalog = load(catalog_path)
    panels = load(panel_path)
    maps = [load(path) for path in recipes]
    libraries = [recipe.get_editor_property('theme_library') for recipe in maps]
    installed = dict(zip(catalog.get_editor_property('source_packages'), catalog.get_editor_property('surfaces')))
    if not all(installed.get(source) for source in sources):
        raise RuntimeError('Installed catalog is missing a reviewed material')

    backup = root / 'Saved/SharedMaterialLibraryBackup/before-matte-deck-panels.zip'
    backup.parent.mkdir(parents=True, exist_ok=True)
    if not backup.exists():
        with zipfile.ZipFile(backup, 'w', zipfile.ZIP_DEFLATED) as archive:
            for package in [catalog_path, panel_path, metal + 'GalvanisedSteel01', retired_path, *recipes, *(library.get_path_name().split('.')[0] for library in libraries)]:
                path = 'Content/' + package.removeprefix('/Game/') + '.uasset'
                if (root / path).exists():
                    archive.write(root / path, path)
    tune_metal = runpy.run_path(str(root / 'scripts/setup_neutral_metal_finishes.py'))['tune']
    tune_metal(panels, installed['/Game/TwinmotionMaterials/Materials/Metal/MI_GalvanisedSteel01'], save)

    for library in libraries:
        themes = list(library.get_editor_property('themes'))
        for theme in themes:
            for role in ['floor', 'deck_steel']:
                finish = theme.get_editor_property(role)
                if finish and finish.get_path_name().split('.')[0] == retired_path:
                    theme.set_editor_property(role, panels)
        library.set_editor_property('themes', themes)
        save(library)

    catalog.set_editor_property('source_packages', sources)
    catalog.set_editor_property('surfaces', [installed[source] for source in sources])
    catalog.set_editor_property('revision', max(4, catalog.get_editor_property('revision')))
    save(catalog)

    approved = set(installed[source] for source in sources)
    for recipe, library in zip(maps, libraries):
        for theme in library.get_editor_property('themes'):
            finishes = list(theme.get_editor_property('materials')) + list(theme.get_editor_property('grounds'))
            finishes += [theme.get_editor_property(role) for role in
                         ['wall', 'floor', 'ceiling', 'shell_steel', 'ladder_steel', 'deck_steel', 'cargo_steel']]
            if any(finish not in approved for finish in finishes):
                raise RuntimeError('Unapproved finish in ' + recipe.get_name())
        print('ER_MATTE_DECK_LAYOUT', recipe.get_name(), recipe.validate_layout(42))

    if assets.does_asset_exist(retired_path):
        refs = assets.find_package_referencers_for_asset(retired_path, True)
        if refs:
            raise RuntimeError('Retired material still referenced by ' + str(list(refs)))
        if not assets.delete_asset(retired_path):
            raise RuntimeError('Could not delete the retired material')
    (root / 'Assets/maps/SourceAssets/SharedMaterials/catalog.json').write_text(json.dumps(manifest, indent=2) + '\n')
    print('ER_MATTE_DECK_PANELS_PASS')


main()
