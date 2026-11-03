"""Unreal-only migration: unbind warehouse geometry from the owned Fire Training recipe.

Back up and validate 516 seeds on a transient candidate, then save only DA_FireTraining.
Existing source art and all levels/materials remain intact.
"""
import hashlib,json,shutil
from pathlib import Path
import unreal as ue
root=Path(ue.Paths.project_dir()).resolve();assets=ue.EditorAssetLibrary
base='/Game/EclipseRecoil/Art/MapTemplates/'
template=assets.load_asset(base+'DA_FireTraining')
if not template:raise RuntimeError('Missing Fire Training recipe')
if assets.get_metadata_tag(template,'ERFireTrainingProceduralRevision')=='1':
    print('ER_FIRE_PROCEDURAL already migrated')
else:
    folder=root/'Assets/maps/Content/EclipseRecoil'
    file=folder/'Art/MapTemplates/DA_FireTraining.uasset'
    sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
    protected=list((folder/'Maps').glob('*.umap'))+[p for p in (folder/'Art/MapTemplates').glob('*.uasset') if p!=file]
    before={str(p):sha(p) for p in protected}
    revision=template.get_editor_property('revision')
    backup=root/'Saved/FireTrainingBackup'/('before-procedural-revision'+str(revision));backup.mkdir(parents=True,exist_ok=True)
    copy=backup/file.name
    if not copy.exists():shutil.copy2(file,copy)
    if sha(copy)!=sha(file):raise RuntimeError('Rollback does not match current recipe')
    candidate=ue.new_object(ue.ERMapTemplate)
    for key in ['template_id','source_reference','archetype','symmetry','arena_size','fire_training','theme_library']:
        candidate.set_editor_property(key,template.get_editor_property(key))
    recipe=candidate.get_editor_property('fire_training')
    for key in ['side_wall_bay','side_wall_lamps','post','end_portal','parapet_module']:recipe.set_editor_property(key,None)
    recipe.set_editor_property('widths',[])
    recipe.set_editor_property('max_aspect',1.5)
    recipe.set_editor_property('min_ceiling_height',800.)
    recipe.set_editor_property('max_ceiling_height',1400.)
    candidate.set_editor_property('fire_training',recipe)
    candidate.set_editor_property('revision',revision+1)
    for seed in list(range(512))+[7919,-17,2147483647,-2147483648]:
        if not candidate.validate_layout(seed):raise RuntimeError('Invalid procedural Fire Training seed '+str(seed))
    template.set_editor_property('fire_training',recipe);template.set_editor_property('revision',revision+1)
    assets.set_metadata_tag(template,'ERFireTrainingProceduralRevision','1')
    if not assets.save_loaded_asset(template,False):raise RuntimeError('Cannot save Fire Training recipe')
    for path,digest in before.items():
        if sha(Path(path))!=digest:raise RuntimeError('Protected asset changed '+path)
    (backup/'receipt.json').write_text(json.dumps(dict(before_revision=revision,after_revision=revision+1,before_sha256=sha(copy),after_sha256=sha(file),validated_seeds=516,protected_assets=len(before)),indent=2)+'\n')
    print('ER_FIRE_PROCEDURAL_SAVED validated_seeds=516 protected_assets='+str(len(before)))
