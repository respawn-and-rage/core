"""Backed-up migration: room/core variety, merged industrial palette, softer cells."""
from pathlib import Path
import hashlib,json,shutil
import unreal as ue
root=Path(ue.Paths.project_dir()).resolve();a=ue.EditorAssetLibrary
base='/Game/EclipseRecoil/Art/'
paths=[base+'MapTemplates/DA_'+n for n in ['Rooms','CentralBuilding','MapThemes']]+[base+'Destruction/DA_ER_BlockMaterials']
objects=[a.load_asset(p) for p in paths]
if not all(objects):raise RuntimeError('Missing variety inputs')
backup=root/'Saved/MapVarietyBackup/2026-10-26';backup.mkdir(parents=True,exist_ok=True)
def file(p):return root/('Assets/maps/Content/'+p.removeprefix('/Game/')+'.uasset')
def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
for path in paths:
    target=backup/file(path).name
    if not target.exists():
        shutil.copy2(file(path),target)
        if sha(target)!=sha(file(path)):raise RuntimeError('Backup verification failed '+path)
protected=[root/'Assets/maps/Content/EclipseRecoil/Art/MapTemplates/DA_FireTraining.uasset',root/'Assets/maps/Content/EclipseRecoil/Art/MapTemplates/DA_MiddleEasternAlleys.uasset']
protected+=list((root/'Assets/maps/Content/EclipseRecoil/Maps').glob('*.umap'))
before={str(p):sha(p) for p in protected}
rooms,central,library,catalog=objects
if a.get_metadata_tag(library,'ERMapVarietyRevision')!='1':
    themes=list(library.get_editor_property('themes'))
    industrial=next(t for t in themes if str(t.get_editor_property('name'))=='Industrial')
    warehouse=next(t for t in themes if str(t.get_editor_property('name'))=='Warehouse')
    desert=next(t for t in themes if str(t.get_editor_property('name'))=='Desert')
    # One combined Industrial/Warehouse palette, plus Desert. All firing-range
    # roles remain distinct, including in the Desert variant.
    for theme in [industrial,desert]:
        for prop in ['shell_steel','ladder_steel','deck_steel']:
            role=warehouse.get_editor_property(prop)
            if not role:raise RuntimeError('Shared Industrial steel roles are missing; restore them from the private asset repository')
            theme.set_editor_property(prop,role)
        materials=list(theme.get_editor_property('materials'))
        materials[5]=warehouse.get_editor_property('materials')[5]
        theme.set_editor_property('materials',materials)
        theme.set_editor_property('preserve_cargo_paint',True)
    grounds=list(industrial.get_editor_property('grounds'))
    for ground in warehouse.get_editor_property('grounds'):
        if ground not in grounds:grounds.append(ground)
    industrial.set_editor_property('grounds',grounds)
    library.set_editor_property('themes',[industrial,desert])
    library.set_editor_property('revision',library.get_editor_property('revision')+1)
    a.set_metadata_tag(library,'ERMapVarietyRevision','1')
# Warm oxidized structural steel contrasts with the cool galvanized access
# pieces even in distant views under the warehouse's blue skylight.
metal=next(row for row in catalog.get_editor_property('materials') if row.get_editor_property('type')==ue.ERBlockMaterial.METAL).get_editor_property('surface')
themes=list(library.get_editor_property('themes'));changed=False
for theme in themes:
    materials=list(theme.get_editor_property('materials'))
    if materials[5]!=metal:materials[5]=metal;changed=True
    theme.set_editor_property('materials',materials)
if changed:
    library.set_editor_property('themes',themes)
    library.set_editor_property('revision',library.get_editor_property('revision')+1)
for obj in [rooms,central]:
    if a.get_metadata_tag(obj,'ERMapVarietyRevision')!='1':
        obj.set_editor_property('revision',obj.get_editor_property('revision')+1)
        a.set_metadata_tag(obj,'ERMapVarietyRevision','1')
# Preserve surface/decal/penetration/transmission references exactly.
health=[13.,65.,390.,26.,650.,65.,5.2]
rows=list(catalog.get_editor_property('materials'))
kinds=[ue.ERBlockMaterial.DRYWALL,ue.ERBlockMaterial.BRICK,ue.ERBlockMaterial.CONCRETE,ue.ERBlockMaterial.WOOD,ue.ERBlockMaterial.STONE,ue.ERBlockMaterial.METAL,ue.ERBlockMaterial.GLASS]
for row in rows:
    kind=row.get_editor_property('type')
    if kind in kinds:row.set_editor_property('max_health',health[kinds.index(kind)])
catalog.set_editor_property('materials',rows)
a.set_metadata_tag(catalog,'ERBlockDamagePresetVersion','2')
for obj in [rooms,central]:
    for seed in [*range(512),7919,-17,2147483647,-2147483648]:
        if not obj.validate_layout(seed):raise RuntimeError('Rejected '+obj.get_name()+' seed='+str(seed))
for obj in objects:
    if not a.save_loaded_asset(obj,False):raise RuntimeError('Cannot save '+obj.get_name())
for p,h in before.items():
    if sha(Path(p))!=h:raise RuntimeError('Protected asset changed '+p)
receipt=dict(result='PASS',validated_seeds_per_recipe=516,health_scale=.65,themes=['Industrial','Desert'],assets={p:sha(file(p)) for p in paths},backup_sha256={p:sha(backup/file(p).name) for p in paths},protected_sha256=before)
(backup/'receipt.json').write_text(json.dumps(receipt,indent=2)+'\n')
(root/'Assets/maps/SourceAssets/MapTemplates/map-variety.json').write_text(json.dumps(receipt,indent=2)+'\n')
print('ER_MAP_VARIETY_MIGRATION PASS')
