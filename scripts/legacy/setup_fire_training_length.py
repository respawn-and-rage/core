"""Fire Training revision2: double the hall length (similar width, 2x floor area).

Explicit migration through Unreal's Python commandlet. Backs up revision1 of
DA_FireTraining privately, changes only FloorArea/MinBays/MaxBays/Revision,
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
if template.get_editor_property('revision')>=2:
    print('ER_FIRE_TRAINING_LENGTH already revision',template.get_editor_property('revision'))
else:
    backup=project/'Saved/FireTrainingBackup/revision1'
    backup.mkdir(parents=True,exist_ok=True)
    target=backup/file.name
    if not target.exists():shutil.copy2(file,target)
    if digest(target)!=digest(file):raise RuntimeError('Backup does not match revision1')
    candidate=ue.new_object(ue.ERMapTemplate)
    for key in ['template_id','source_reference','archetype','symmetry','arena_size','fire_training']:candidate.set_editor_property(key,template.get_editor_property(key))
    recipe=candidate.get_editor_property('fire_training')
    recipe.set_editor_property('floor_area',ue.Vector2D(1360,1840))
    recipe.set_editor_property('min_bays',14);recipe.set_editor_property('max_bays',28)
    candidate.set_editor_property('fire_training',recipe);candidate.set_editor_property('revision',2)
    for seed in list(range(512))+[7919,-17,2147483647,-2147483648]:
        if not candidate.validate_layout(seed):raise RuntimeError('Invalid Fire Training seed '+str(seed))
    for key in ['revision','fire_training']:template.set_editor_property(key,candidate.get_editor_property(key))
    assets.set_metadata_tag(template,'ERFireTrainingLengthRevision','2')
    if not assets.save_loaded_asset(template,False):raise RuntimeError('Cannot save template')
    (backup/'receipt.json').write_text(json.dumps(dict(revision1_sha256=digest(target),revision2_sha256=digest(file),floor_area=[1360,1840],bays=[14,28]),indent=2)+'\n')
    print('ER_FIRE_TRAINING_LENGTH_SAVED seeds=516')
