"""Remove superseded material sources through Unreal after dependency checks.

The owner authorized Git rollback instead of another disk copy. Tracked deletions
must match HEAD and have verified Git/LFS payloads. The raw Twinmotion download
can be acquired again from the owner's Fab library.
Run the maintenance editor with
``-ini:Engine:[/Script/Engine.RendererSettings]:r.GenerateMeshDistanceFields=False``
to avoid rebuilding the vendor showroom meshes while deleting them.
"""
from pathlib import Path
import hashlib
import json
import subprocess
import unreal as ue


def main():
    root = Path(__file__).resolve().parents[1]
    out = root/'Saved/Testing/MaterialAudit'
    out.mkdir(parents=True, exist_ok=True)
    receipt = json.loads((out/'shared-library-receipt.json').read_text())
    if receipt.get('result') != 'PASS' or receipt.get('migration_revision') not in {'2', '3', '4'}:
        raise RuntimeError('Complete the shared shader migration before removing sources')
    a = ue.EditorAssetLibrary
    registry = ue.AssetRegistryHelpers.get_asset_registry()
    registry.search_all_assets(True)
    registry.wait_for_completion()
    options = ue.AssetRegistryDependencyOptions(
        include_hard_package_references=True, include_soft_package_references=True,
        include_searchable_names=False, include_soft_management_references=True,
        include_hard_management_references=True)
    assets = registry.get_all_assets(True)
    packages = {str(row.package_name) for row in assets if str(row.package_name).startswith('/Game/')}
    vendor_preview = {'/Game/Developers/mitchellwilson/TestLighting'} & packages
    raw = {p for p in packages if p.startswith('/Game/TwinmotionMaterials/')} | vendor_preview
    prefixes = tuple('/Game/EclipseRecoil/Art/'+p+'/' for p in [
        'ProceduralTextures', 'Industrial/SketchfabTextures',
        'FactoryEnvironment/Materials', 'FactoryEnvironment/Textures',
        'MiddleEasternAlleys/Materials', 'MiddleEasternAlleys/Textures'])
    classes = {'Material', 'MaterialInstanceConstant', 'MaterialFunction', 'Texture2D', 'TextureCube'}
    legacy = {str(row.package_name) for row in assets
              if str(row.package_name).startswith(prefixes) and str(row.asset_class_path.asset_name) in classes}
    candidates = raw | legacy
    candidates |= {'/Game/EclipseRecoil/Art/Industrial/SharedSteel/M_ER_IndustrialLampEmissive'} & packages
    dependencies = {p: {str(d) for d in registry.get_dependencies(p, options)} for p in packages}
    tree_manifest = root/'Saved/Testing/CeilingPanels/removed-trees.json'
    if tree_manifest.exists():
        pending_trees = [p.split('.')[0] for p in json.loads(tree_manifest.read_text())]
        while pending_trees:
            package = pending_trees.pop()
            if package in candidates or not package.startswith('/Game/EclipseRecoil/Art/') or '/SharedMaterials/' in package:
                continue
            if package not in packages:
                continue
            candidates.add(package)
            pending_trees.extend(dependencies[package])
    # Retain the transitive closure of every reference from outside this scope,
    # including old mesh slots, foliage, emissive fixtures and sky.
    retained = set()
    pending = [d for p in packages-candidates for d in dependencies[p] if d in candidates]
    while pending:
        package = pending.pop()
        if package in retained:
            continue
        retained.add(package)
        pending.extend(dependencies[package] & candidates)
    if raw & retained:
        blockers = {p: sorted(dependencies[p] & raw) for p in packages-candidates if dependencies[p] & raw}
        (out/'raw-material-reference-blockers.json').write_text(json.dumps(blockers, indent=2)+'\n')
        raise RuntimeError('Raw download still has outside references: '+str(sorted(raw & retained)))
    targets = candidates-retained
    repo = root/'Assets/maps'
    def git(*args):
        return subprocess.check_output(['git', '-C', str(repo), *args])
    revision = git('rev-parse', 'HEAD').decode().strip()
    tracked = set(git('ls-files', '-z').decode().split('\0'))
    media = next(line.split('=', 1)[1] for line in git('lfs', 'env').decode().splitlines() if line.startswith('LocalMediaDir='))
    def disk(package):
        file = root/('Content/'+package.removeprefix('/Game/')+'.uasset')
        return file if file.exists() else file.with_suffix('.umap')
    def sha(file):
        return hashlib.sha256(file.read_bytes()).hexdigest()
    verified = []
    for package in sorted(targets-raw):
        relative = 'Content/'+package.removeprefix('/Game/')+'.uasset'
        if relative not in tracked:
            raise RuntimeError('No Git rollback for '+relative)
        blob = git('show', 'HEAD:'+relative)
        if blob.startswith(b'version https://git-lfs.github.com/spec/v1'):
            oid = next(line.split(':', 1)[1] for line in blob.decode().splitlines() if line.startswith('oid sha256:'))
            payload = Path(media)/oid[:2]/oid[2:4]/oid
            if not payload.is_file() or sha(payload) != oid or sha(disk(package)) != oid:
                raise RuntimeError('Git LFS rollback is unavailable or asset changed: '+relative)
        elif hashlib.sha256(blob).hexdigest() != sha(disk(package)):
            raise RuntimeError('Asset differs from Git rollback: '+relative)
        verified.append(relative)
    protected = {}
    for file in (repo/'Content').rglob('*'):
        if file.is_file() and file.suffix in {'.uasset', '.umap'}:
            package = '/Game/'+file.relative_to(repo/'Content').with_suffix('').as_posix()
            if package not in targets:
                protected[file] = sha(file)
    freed = sum(disk(p).stat().st_size for p in targets if disk(p).is_file())
    plan = {'git_revision': revision, 'git_verified_files': verified,
            'fab_download_packages': len(raw), 'removed_packages': sorted(targets),
            'retained_referenced_packages': sorted(retained), 'removed_bytes': freed}
    (out/'retired-material-sources-plan.json').write_text(json.dumps(plan, indent=2)+'\n')
    for package in vendor_preview:
        if not a.delete_asset(package):
            raise RuntimeError('Unreal could not remove vendor preview '+package)
    if raw-vendor_preview and not a.delete_directory('/Game/TwinmotionMaterials'):
        # A demo world can leave small packages on disk after bulk deletion.
        leftovers = list((root/'Content/TwinmotionMaterials').rglob('*.uasset'))
        leftovers += list((root/'Content/TwinmotionMaterials').rglob('*.umap'))
        registry.scan_files_synchronous([str(p) for p in leftovers], force_rescan=True)
        for file in leftovers:
            package = '/Game/'+file.relative_to(root/'Content').with_suffix('').as_posix()
            if package not in targets or package in retained:
                raise RuntimeError('Remaining file is outside the verified deletion plan: '+package)
            outside = {str(p) for p in registry.get_referencers(package, options)}-targets
            if outside:
                raise RuntimeError('Remaining source gained outside references: '+package+' '+str(sorted(outside)))
            if not a.delete_asset(package):
                raise RuntimeError('Unreal could not remove remaining source '+package)
    for package in sorted(targets-raw):
        if a.does_asset_exist(package) and not a.delete_asset(package):
            raise RuntimeError('Unreal could not remove '+package)
    for file, digest in protected.items():
        if not file.is_file() or sha(file) != digest:
            raise RuntimeError('Protected package changed: '+str(file))
    remaining = {p: disk(p).stat().st_size for p in sorted(targets) if disk(p).is_file()}
    plan['remaining_packages_on_disk'] = remaining
    plan['removed_bytes'] = freed-sum(remaining.values())
    plan['result'] = 'PARTIAL' if remaining else 'PASS'
    plan['protected_packages'] = len(protected)
    (out/'retired-material-sources-receipt.json').write_text(json.dumps(plan, indent=2)+'\n')
    print('ER_RETIRED_MATERIAL_SOURCES_'+plan['result']+' '+str({'packages': len(targets)-len(remaining), 'bytes': plan['removed_bytes']}))


main()
