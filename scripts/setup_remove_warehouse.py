"""Retire the warehouse kit via Unreal, with audited references and external rollback."""
from pathlib import Path
import hashlib,json,tarfile,shutil
import unreal as ue
root=Path(ue.Paths.project_dir()).resolve(); maps=root/'Assets/maps'; a=ue.EditorAssetLibrary
art='/Game/EclipseRecoil/Art/'; old=art+'FireTraining/'; shared=art+'Industrial/SharedSteel/'
r=ue.AssetRegistryHelpers.get_asset_registry();r.search_all_assets(True)
opts=ue.AssetRegistryDependencyOptions(True,True,True,True,True)
candidates={str(x.package_name):str(x.asset_class_path.asset_name) for x in r.get_assets_by_path(old.rstrip('/'),recursive=True) if '/Procedural/' not in str(x.package_name)}
report_path=root/'Saved/Testing/warehouse-removal.json';report_path.parent.mkdir(parents=True,exist_ok=True)
if not candidates:
    print('ER_WAREHOUSE_REMOVAL already complete');raise SystemExit
refs={p:sorted(str(x) for x in r.get_referencers(p,opts) if str(x) not in candidates) for p in candidates}
external=set(x for xs in refs.values() for x in xs)
if any(refs[p] for p,c in candidates.items() if c not in ('Material','MaterialInstanceConstant')):
    raise RuntimeError('Mesh/texture has outside references '+json.dumps(refs))
def disk(p):return maps/'Content'/(p.removeprefix('/Game/')+'.uasset' if not p.startswith('/Game/EclipseRecoil/Maps/') else p.removeprefix('/Game/')+'.umap')
def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
sources=maps/'SourceAssets/FireTraining'
source_files=[p for p in sources.rglob('*') if p.is_file() and 'Procedural' not in p.relative_to(sources).parts]
files={disk(p) for p in set(candidates)|external}|set(source_files)
if any(not p.exists() for p in files):raise RuntimeError('Missing backup input '+str([str(p) for p in files if not p.exists()]))
backup=root.parent/'_backups/warehouse-removal-20261103.tar.gz'
backup.parent.mkdir(exist_ok=True)
if backup.exists():raise RuntimeError('Existing rollback: inspect before retry')
hashes={str(p.relative_to(root)):sha(p) for p in files}
with tarfile.open(backup,'w:gz',compresslevel=1) as z:
    for p in sorted(files):z.add(p,arcname=str(p.relative_to(root)),recursive=False)
with tarfile.open(backup) as z:
    for m in z.getmembers():
        if hashlib.sha256(z.extractfile(m).read()).hexdigest()!=hashes[m.name]:raise RuntimeError('Bad rollback '+m.name)
protected={str(p):sha(p) for p in (maps/'Content').rglob('*') if p.is_file() and p not in files}
report=dict(backup=str(backup),backup_sha256=sha(backup),backup_files=hashes,candidates=candidates,external_references=refs,result='BACKUP_VERIFIED')
report_path.write_text(json.dumps(report,indent=2))
def load(p):
    obj=a.load_asset(p)
    if not obj:raise RuntimeError('Cannot load '+p)
    return obj
def save(obj):
    if not a.save_loaded_asset(obj,False):raise RuntimeError('Cannot save '+obj.get_path_name())
# Load every outside referencer before replacement, including level-owned defaults.
loaded=[load(p) for p in sorted(external)]
replacements={old+'M_ER_WH_WetConcrete':art+'Industrial/SketchfabTextures/M_ER_SF_concrete_seamless'}
for suffix,new_name in [('RoleIron','M_ER_SharedIron'),('RoleGalvanized','M_ER_SharedGalvanized'),('WallPlates','MI_ER_IndustrialWallPlates'),('DeckPlates','MI_ER_IndustrialDeckPlates'),('LadderGalvanized','MI_ER_IndustrialLadderGalvanized'),('RoofSteel','MI_ER_IndustrialRoofSteel')]:
    prefix='M_ER_WH_' if suffix.startswith('Role') else 'MI_ER_WH_'
    src=old+'WarehouseTheme/'+prefix+suffix;dst=shared+new_name
    obj=a.duplicate_asset(src,dst)
    if not obj:raise RuntimeError('Cannot duplicate '+src)
    save(obj);replacements[src]=dst
for src,dst,source in [(old+'M_ER_WH_Metal',shared+'M_ER_IndustrialShellSteel',old+'Procedural/M_ER_FireTraining_CeilingSteel'),(old+'M_ER_WH_LampEmissive',shared+'M_ER_IndustrialLampEmissive',old+'M_ER_WH_LampEmissive')]:
    obj=a.duplicate_asset(source,dst)
    if not obj:raise RuntimeError('Cannot duplicate '+source)
    save(obj);replacements[src]=dst
# Consolidation updates references, including the copied material-instance parents.
for src,dst in replacements.items():
    a.consolidate_assets(load(dst),[load(src)])
for obj in loaded:save(obj)
for dst in replacements.values():
    if dst.startswith(shared):save(load(dst))
r.scan_paths_synchronous(['/Game/EclipseRecoil'],True)
# Redirectors may retain references from kit-internal meshes; only outside refs matter.
after={p:sorted(str(x) for x in r.get_referencers(p,opts) if str(x) not in candidates) for p in candidates}
if any(after.values()):
    report.update(result='REFERENCES_REMAIN',remaining=after);report_path.write_text(json.dumps(report,indent=2));raise RuntimeError('Outside references remain '+json.dumps(after))
for p in sorted(candidates,key=lambda p:candidates[p] not in ('StaticMesh',)):
    if a.does_asset_exist(p) and not a.delete_asset(p):raise RuntimeError('Cannot delete '+p)
for p in source_files:p.unlink()
for p in sorted((p for p in sources.rglob('*') if p.is_dir()),key=lambda p:len(p.parts),reverse=True):
    if not any(p.iterdir()):p.rmdir()
for p,digest in protected.items():
    if sha(Path(p))!=digest:raise RuntimeError('Protected file changed '+p)
report.update(result='PASS',replacements=replacements,protected_files_verified=len(protected))
report_path.write_text(json.dumps(report,indent=2)+'\n')
print('ER_WAREHOUSE_REMOVAL_PASS packages='+str(len(candidates)))
