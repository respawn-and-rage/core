"""Verify the shared finish library and retire the raw Twinmotion import."""
import hashlib
import json
from pathlib import Path
import runpy
import tempfile
import zipfile

import unreal as ue

root = Path(__file__).resolve().parents[1]
content = root / 'Content'
raw = content / 'TwinmotionMaterials'
files = sorted(file for file in raw.rglob('*') if file.is_file())
packages = {
    '/Game/' + file.relative_to(content).with_suffix('').as_posix(): file
    for file in files if file.suffix in {'.uasset', '.umap'}
}
if len(files) != len(packages):
    raise RuntimeError('Unexpected files in the raw import; inspect them before deletion')

assets = ue.EditorAssetLibrary
registry = ue.AssetRegistryHelpers.get_asset_registry()
registry.search_all_assets(True)
registry.wait_for_completion()
if files:
    registry.scan_files_synchronous([str(file) for file in files], force_rescan=True)
options = ue.AssetRegistryDependencyOptions(
    include_hard_package_references=True, include_soft_package_references=True,
    include_searchable_names=False, include_soft_management_references=True,
    include_hard_management_references=True)

base = '/Game/EclipseRecoil/Art/SharedMaterials/'
catalog = assets.load_asset(base + 'DA_SharedMapSurfaces')
sources = runpy.run_path(str(root / 'scripts/shared_material_catalog.py'))['CATALOG']['families']
expected = sorted({path for family in sources.values() for path in family})
surfaces = list(catalog.get_editor_property('surfaces'))
if list(catalog.get_editor_property('source_packages')) != expected or len(set(surfaces)) != len(expected):
    raise RuntimeError('Installed catalog differs from the reviewed manifest')
if any(not surface or not surface.get_path_name().startswith(base) for surface in surfaces):
    raise RuntimeError('Missing or unowned catalog surface')

for name in ['CentralBuilding', 'MiddleEasternAlleys', 'FireTraining', 'Rooms', 'Village', 'Town']:
    recipe = assets.load_asset('/Game/EclipseRecoil/Art/MapTemplates/DA_' + name)
    library = recipe.get_editor_property('theme_library')
    if library.get_editor_property('shared_surfaces') != catalog or not recipe.validate_layout(42):
        raise RuntimeError('Invalid template binding or layout: ' + name)

closure = set()
pending = [surface.get_path_name().split('.')[0] for surface in surfaces]
while pending:
    package = pending.pop()
    if package in closure or not package.startswith('/Game/'):
        continue
    if package.startswith('/Game/TwinmotionMaterials/') or not assets.does_asset_exist(package):
        raise RuntimeError('Missing or raw catalog dependency: ' + package)
    closure.add(package)
    pending.extend(str(dep) for dep in registry.get_dependencies(package, options))

for package in packages:
    outside = set(map(str, registry.get_referencers(package, options))) - packages.keys()
    if outside:
        raise RuntimeError(f'Raw package {package} referenced by {sorted(outside)}')

def digest(file):
    return hashlib.sha256(file.read_bytes()).hexdigest()

protected = [content / (package.removeprefix('/Game/') + '.uasset') for package in closure]
protected.append(content / (base.removeprefix('/Game/') + 'DA_SharedMapSurfaces.uasset'))
hashes = {file: digest(file) for file in protected}
size = sum(file.stat().st_size for file in files)
backup = None
if packages:
    with tempfile.NamedTemporaryFile(prefix='er-twinmotion-rollback-', suffix='.zip', delete=False) as archive:
        backup = archive.name
    raw_hashes = {file.relative_to(content).as_posix(): digest(file) for file in files}
    with zipfile.ZipFile(backup, 'w', zipfile.ZIP_DEFLATED) as archive:
        for file in files:
            archive.write(file, file.relative_to(content))
    with zipfile.ZipFile(backup) as archive:
        if any(hashlib.sha256(archive.read(name)).hexdigest() != expected_hash
               for name, expected_hash in raw_hashes.items()):
            raise RuntimeError('Raw pack backup verification failed')
    print('ER_TWINMOTION_ROLLBACK ' + backup)
    # Retire showroom worlds before their preview meshes.
    for package, file in packages.items():
        if file.suffix != '.umap':
            continue
        assets.delete_asset(package)
        if file.exists():
            # The editor can leave a stale showroom package on disk after deletion.
            retired = package + '_Retired'
            if assets.does_asset_exist(retired) or not assets.rename_asset(package, retired) or not assets.delete_asset(retired):
                raise RuntimeError('Unreal could not remove showroom ' + package)
    ue.SystemLibrary.collect_garbage()
    assets.delete_directory('/Game/TwinmotionMaterials')
    for package, file in packages.items():
        if file.exists() and not assets.delete_asset(package):
            raise RuntimeError('Unreal could not remove ' + package)

if any(file.is_file() for file in raw.rglob('*')):
    raise RuntimeError('Raw pack files remain on disk')
if any(not file.is_file() or digest(file) != expected_hash for file, expected_hash in hashes.items()):
    raise RuntimeError('A protected shared asset changed')
print('ER_TWINMOTION_PACK_REMOVED ' + json.dumps({
    'retired_packages': len(packages), 'retired_bytes': size, 'rollback': backup,
    'preserved_surfaces': len(surfaces), 'protected_packages': len(hashes),
}))
