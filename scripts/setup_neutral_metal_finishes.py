"""Tune the owned panel and galvanized finishes without changing palette roles."""
from pathlib import Path
import zipfile

import unreal as ue


def tune(panels, galvanized, save):
    edit = ue.MaterialEditingLibrary
    changed = False
    for material, scalars, tint in [
        (panels, {'Roughness Min': .8, 'Physical Width': 150., 'Physical Height': 150.},
         ue.LinearColor(.42, .42, .42, 0)),
        (galvanized, {'Roughness Min': .75}, None),
    ]:
        dirty = False
        for name, value in scalars.items():
            if abs(edit.get_material_instance_scalar_parameter_value(material, name) - value) < .0001:
                continue
            edit.set_material_instance_scalar_parameter_value(material, name, value)
            if abs(edit.get_material_instance_scalar_parameter_value(material, name) - value) > .0001:
                raise RuntimeError('Cannot set ' + name + ' on ' + material.get_name())
            dirty = True
        if tint is not None:
            current = edit.get_material_instance_vector_parameter_value(material, 'Tint')
            if any(abs(getattr(current, channel) - getattr(tint, channel)) > .0001 for channel in 'rgba'):
                edit.set_material_instance_vector_parameter_value(material, 'Tint', tint)
                actual = edit.get_material_instance_vector_parameter_value(material, 'Tint')
                if any(abs(getattr(actual, channel) - getattr(tint, channel)) > .0001 for channel in 'rgba'):
                    raise RuntimeError('Cannot neutralize the panel tint')
                dirty = True
        if dirty:
            edit.update_material_instance(material)
            save(material)
            changed = True
    return changed


def main():
    root = Path(__file__).resolve().parents[1]
    assets = ue.EditorAssetLibrary
    base = '/Game/EclipseRecoil/Art/SharedMaterials/'
    catalog = assets.load_asset(base + 'DA_SharedMapSurfaces')
    by_name = {material.get_name(): material for material in catalog.get_editor_property('surfaces')}
    panels = by_name['MI_AluPanels150x150']
    galvanized = by_name['MI_GalvanisedSteel01']
    backup = root / 'Saved/SharedMaterialLibraryBackup/before-neutral-metal-finishes.zip'
    backup.parent.mkdir(parents=True, exist_ok=True)
    if not backup.exists():
        with zipfile.ZipFile(backup, 'w', zipfile.ZIP_DEFLATED) as archive:
            for material in [catalog, panels, galvanized]:
                package = material.get_path_name().split('.')[0]
                path = 'Content/' + package.removeprefix('/Game/') + '.uasset'
                archive.write(root / path, path)

    def save(material):
        if not assets.save_loaded_asset(material, False):
            raise RuntimeError('Could not save ' + material.get_path_name())

    tune(panels, galvanized, save)
    if catalog.get_editor_property('revision') < 4:
        catalog.set_editor_property('revision', 4)
        save(catalog)
    print('ER_NEUTRAL_METAL_FINISHES_PASS')


if __name__ == '__main__':
    main()
