"""Fire Training revision3: multi-nave width (1–4 naves), taller (2–4 bands),
longer (14–36 bays) halls with reserved empty plazas.

Explicit migration through Unreal's Python commandlet. Backs up revision2 of
DA_FireTraining privately, changes only the recipe size fields and Revision,
validates 516 seeds, then saves. Kit, level and other maps stay unchanged.
"""
import hashlib
import json
import shutil
from pathlib import Path
import unreal as ue

project=Path(ue.Paths.project_dir()).resolve()
assets=ue.EditorAssetLibrary
path='/Game/EclipseRecoil/Art/MapTemplates/DA_FireTraining'
file=project/'Assets/maps/Content/EclipseRecoil/Art/MapTemplates/DA_FireTraining.uasset'
digest=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
template=assets.load_asset(path)
if not template:raise RuntimeError('Run setup_fire_training.py first')
revision=template.get_editor_property('revision')
if revision>=3:
    print('ER_FIRE_TRAINING_SCALE already revision',revision)
elif revision!=2:
    raise RuntimeError('Apply setup_fire_training_length.py (revision2) first')
else:
    backup=project/'Saved/FireTrainingBackup/revision2'
    backup.mkdir(parents=True,exist_ok=True)
    target=backup/file.name
    if not target.exists():shutil.copy2(file,target)
    if digest(target)!=digest(file):raise RuntimeError('Backup does not match revision2')
    candidate=ue.new_object(ue.ERMapTemplate)
    for key in ['template_id','source_reference','archetype','symmetry','arena_size','fire_training']:candidate.set_editor_property(key,template.get_editor_property(key))
    recipe=candidate.get_editor_property('fire_training')
    for key,value in [('floor_area',ue.Vector2D(1300,7000)),('min_bays',14),('max_bays',36),('min_naves',1),('max_naves',4),
                      ('max_aspect',7.0),('max_plazas',3),('min_height_bands',2),('max_height_bands',4)]:recipe.set_editor_property(key,value)
    candidate.set_editor_property('fire_training',recipe);candidate.set_editor_property('revision',3)
    for seed in list(range(512))+[7919,-17,2147483647,-2147483648]:
        if not candidate.validate_layout(seed):raise RuntimeError('Invalid Fire Training seed '+str(seed))
    for key in ['revision','fire_training']:template.set_editor_property(key,candidate.get_editor_property(key))
    assets.set_metadata_tag(template,'ERFireTrainingScaleRevision','3')
    if not assets.save_loaded_asset(template,False):raise RuntimeError('Cannot save template')
    (backup/'receipt.json').write_text(json.dumps(dict(revision2_sha256=digest(target),revision3_sha256=digest(file),floor_area=[1300,7000],
        bays=[14,36],naves=[1,4],bands=[2,4],max_aspect=7,max_plazas=3),indent=2)+'\n')
    print('ER_FIRE_TRAINING_SCALE_SAVED seeds=516')
