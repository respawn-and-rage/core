"""Restore both visible sides of Village's imported timber shells in Unreal."""
from pathlib import Path
import hashlib
import shutil

import unreal as ue


def enable_two_sided(finish):
    overrides = finish.get_editor_property('base_property_overrides')
    if not overrides.get_editor_property('override_two_sided') or not overrides.get_editor_property('two_sided'):
        overrides.set_editor_property('override_two_sided', True)
        overrides.set_editor_property('two_sided', True)
        finish.set_editor_property('base_property_overrides', overrides)
        ue.MaterialEditingLibrary.update_material_instance(finish)


def main():
    assets = ue.EditorAssetLibrary
    recipe = assets.load_asset('/Game/EclipseRecoil/Art/MapTemplates/DA_Village')
    library = recipe.get_editor_property('theme_library')
    catalog = library.get_editor_property('shared_surfaces')
    finish, = [surface for surface in catalog.get_editor_property('surfaces')
               if surface.get_name() == 'MI_RoughPlanks']
    if not isinstance(finish, ue.MaterialInstanceConstant):
        raise RuntimeError('Village timber finish must be a material instance')

    content = Path(ue.Paths.convert_relative_path_to_full(ue.Paths.project_content_dir()))

    def package(asset):
        return content / (asset.get_path_name().split('.')[0].removeprefix('/Game/') + '.uasset')

    protected = {path: hashlib.sha256(path.read_bytes()).digest()
                 for path in map(package, [recipe, library, catalog, finish.get_editor_property('parent')])}
    overrides = finish.get_editor_property('base_property_overrides')
    if not overrides.get_editor_property('override_two_sided') or not overrides.get_editor_property('two_sided'):
        path = package(finish)
        backup = Path(ue.Paths.convert_relative_path_to_full(ue.Paths.project_saved_dir())) / (
            'SharedMaterialLibraryBackup/MI_RoughPlanks.before-two-sided.uasset')
        if not backup.exists():
            backup.parent.mkdir(parents=True, exist_ok=True)
            shutil.copyfile(path, backup)
            if hashlib.sha256(backup.read_bytes()).digest() != hashlib.sha256(path.read_bytes()).digest():
                raise RuntimeError('Village timber finish backup did not verify')
        enable_two_sided(finish)
        if not assets.save_loaded_asset(finish):
            raise RuntimeError('Could not save Village timber finish')

    for path, digest in protected.items():
        if hashlib.sha256(path.read_bytes()).digest() != digest:
            raise RuntimeError(f'Protected package changed: {path}')
    ue.log('ER_VILLAGE_TWO_SIDED_WOOD_OK')


if __name__ == '__main__':
    main()
