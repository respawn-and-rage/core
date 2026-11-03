"""Share Industrial, Desert and Jungle across all five procedural templates.

Only the owned theme library is saved. Existing materials, recipes, map levels
and damage calibration are protected by hashes; a verified ZIP retains rollback.
"""
from pathlib import Path
import hashlib,json,zipfile
import unreal as ue
root=Path(ue.Paths.project_dir()).resolve();private=root/'Assets/maps';a=ue.EditorAssetLibrary
base='/Game/EclipseRecoil/Art/MapTemplates/'
library=a.load_asset(base+'DA_MapThemes')
if not library:raise RuntimeError('Missing shared theme library')
file=private/'Content/EclipseRecoil/Art/MapTemplates/DA_MapThemes.uasset'
sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
protected=[p for p in (private/'Content/EclipseRecoil/Art/MapTemplates').glob('*.uasset') if p!=file]
protected+=list((private/'Content/EclipseRecoil/Maps').glob('*.umap'))
protected+=[private/'Content/EclipseRecoil/Art/Destruction/DA_ER_BlockMaterials.uasset']
protected+=list((private/'Content/EclipseRecoil/Art/Village').rglob('*.uasset'))+list((private/'Content/EclipseRecoil/Art/Industrial/SharedSteel').rglob('*.uasset'))
before={str(p):sha(p) for p in protected}
backup=root/'Saved/MapThemesBackup/before-shared-jungle.zip';backup.parent.mkdir(parents=True,exist_ok=True)
if not backup.exists():
    with zipfile.ZipFile(backup,'w',zipfile.ZIP_DEFLATED) as z:z.write(file,file.name);z.writestr('hashes.json',json.dumps({file.name:sha(file)}))
with zipfile.ZipFile(backup) as z:
    for name,digest in json.loads(z.read('hashes.json')).items():
        if hashlib.sha256(z.read(name)).hexdigest()!=digest:raise RuntimeError('Invalid theme rollback')
props=['wall','floor','ceiling','shell_steel','ladder_steel','deck_steel']
def identity(theme):
    path=lambda obj:obj.get_path_name() if obj else None
    return dict(name=str(theme.get_editor_property('name')),materials=[path(x) for x in theme.get_editor_property('materials')],grounds=[path(x) for x in theme.get_editor_property('grounds')],roles={k:path(theme.get_editor_property(k)) for k in props},cargo=theme.get_editor_property('preserve_cargo_paint'))
themes=list(library.get_editor_property('themes'));original={str(t.get_editor_property('name')):identity(t) for t in themes}
if set(original)!={'Industrial','Desert','Jungle'}:raise RuntimeError('Unexpected palette ownership')
jungle=next(t for t in themes if str(t.get_editor_property('name'))=='Jungle')
for key,path in dict(shell_steel='/Game/EclipseRecoil/Art/Village/Topology/M_Village_GraySheet',ladder_steel='/Game/EclipseRecoil/Art/Industrial/SharedSteel/MI_ER_IndustrialLadderGalvanized',deck_steel='/Game/EclipseRecoil/Art/Industrial/SharedSteel/MI_ER_IndustrialDeckPlates').items():
    obj=a.load_asset(path)
    if not obj:raise RuntimeError('Missing theme role '+path)
    jungle.set_editor_property(key,obj)
jungle.set_editor_property('preserve_cargo_paint',True)
library.set_editor_property('themes',themes)
for theme in library.get_editor_property('themes'):
    name=str(theme.get_editor_property('name'))
    if name!='Jungle' and identity(theme)!=original[name]:raise RuntimeError('Existing palette changed '+name)
for name in ['CentralBuilding','MiddleEasternAlleys','FireTraining','Rooms','Village']:
    template=a.load_asset(base+'DA_'+name)
    if not template or template.get_editor_property('theme_library')!=library:raise RuntimeError('Unbound template '+name)
    for seed in range(32):
        if not template.validate_layout(seed):raise RuntimeError('Invalid shared theme '+name+' seed '+str(seed))
if a.get_metadata_tag(library,'ERAllTemplateThemesRevision')!='1':
    library.set_editor_property('revision',library.get_editor_property('revision')+1)
    a.set_metadata_tag(library,'ERAllTemplateThemesRevision','1')
    if not a.save_loaded_asset(library,False):raise RuntimeError('Cannot save shared themes')
for name,digest in before.items():
    if sha(Path(name))!=digest:raise RuntimeError('Protected asset changed '+name)
receipt=dict(result='PASS',revision=library.get_editor_property('revision'),marker=1,templates=5,seeds_per_template=32,themes=[identity(t) for t in library.get_editor_property('themes')],backup=str(backup),library_sha256=sha(file),protected_sha256=before)
(private/'SourceAssets/MapTemplates/shared-jungle.json').write_text(json.dumps(receipt,indent=2)+'\n')
print('ER_SHARED_JUNGLE_SETUP_COMPLETE templates=5 themes=3 seeds=160 protected='+str(len(before)))
