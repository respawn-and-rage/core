"""Unreal-only Fire Training lighting: neutral ambient fill and brighter indoor lights.

Save only L_FireTraining and an owned constant-white HDR cubemap. Back up the level
and verify all other levels, palettes and recipes remain byte-identical.
"""
from pathlib import Path
import hashlib,json,shutil
import unreal as ue
root=Path(ue.Paths.project_dir()).resolve();a=ue.EditorAssetLibrary
level='/Game/EclipseRecoil/Maps/L_FireTraining';folder='/Game/EclipseRecoil/Art/FireTraining/Procedural'
private=root/'Assets/maps';file=private/'Content/EclipseRecoil/Maps/L_FireTraining.umap'
sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
protected=[p for p in (private/'Content/EclipseRecoil/Maps').glob('*.umap') if p!=file]+list((private/'Content/EclipseRecoil/Art/MapTemplates').glob('*.uasset'))
before={str(p):sha(p) for p in protected}
backup=root/'Saved/FireTrainingBackup/before-neutral-lighting';backup.mkdir(parents=True,exist_ok=True)
copy=backup/file.name
if not copy.exists():shutil.copy2(file,copy)
# A generated neutral radiance field replaces the blue atmospheric capture.
source=private/'SourceAssets/FireTraining/Procedural/NeutralAmbient.hdr';source.parent.mkdir(parents=True,exist_ok=True)
row=b'\x02\x02\x00\x20'+b''.join(bytes([160,v]) for v in [128,128,128,129])
data=b'#?RADIANCE\nFORMAT=32-bit_rle_rgbe\n\n-Y 16 +X 32\n'+row*16
if source.exists() and source.read_bytes()!=data:raise RuntimeError('Unexpected neutral HDR source')
if not source.exists():source.write_bytes(data)
path=folder+'/T_ER_FireTraining_NeutralAmbient';a.make_directory(folder)
if a.does_asset_exist(path):
    cube=a.load_asset(path)
    if a.get_metadata_tag(cube,'ERFireTrainingLightingOwned')!='1':raise RuntimeError('Unowned cubemap')
else:
    task=ue.AssetImportTask();task.set_editor_property('filename',str(source));task.set_editor_property('destination_path',folder);task.set_editor_property('destination_name','T_ER_FireTraining_NeutralAmbient');task.set_editor_property('automated',True);task.set_editor_property('save',False);task.set_editor_property('factory',ue.TextureFactory())
    ue.AssetToolsHelpers.get_asset_tools().import_asset_tasks([task]);cube=a.load_asset(path)
    if not isinstance(cube,ue.TextureCube):raise RuntimeError('Neutral HDR did not import as a cubemap')
    cube.set_editor_property('srgb',False);a.set_metadata_tag(cube,'ERFireTrainingLightingOwned','1')
    if not a.save_loaded_asset(cube,False):raise RuntimeError('Cannot save neutral cubemap')
if not ue.EditorLoadingAndSavingUtils.load_map(level):raise RuntimeError('Cannot load Fire Training')
w=ue.get_editor_subsystem(ue.UnrealEditorSubsystem).get_editor_world()
skies=list(ue.GameplayStatics.get_all_actors_of_class(w,ue.SkyLight))
if len(skies)!=1:raise RuntimeError('Unexpected SkyLight count')
c=skies[0].light_component
for key,value in [('source_type',ue.SkyLightSourceType.SLS_SPECIFIED_CUBEMAP),('cubemap',cube),('real_time_capture',False),('light_color',ue.Color(r=255,g=255,b=255,a=255)),('intensity',.55),('lower_hemisphere_is_black',False),('cast_shadows',False)]:c.set_editor_property(key,value)
if not ue.EditorLoadingAndSavingUtils.save_current_level():raise RuntimeError('Cannot save Fire Training lighting')
for p,digest in before.items():
    if sha(Path(p))!=digest:raise RuntimeError('Protected asset changed '+p)
art='/Game/EclipseRecoil/Art/'
options={
 'cemento_asciutto_chiaro':art+'Industrial/SketchfabTextures/M_ER_SF_concrete_seamless',
 'cemento_ruvido':art+'Industrial/SketchfabTextures/M_ER_SF_concrete_2k',
 'asfalto_opaco':art+'ProceduralTextures/M_ER_PBR_asphalt_02',
 'piastre_acciaio':art+'Industrial/SharedSteel/MI_ER_IndustrialDeckPlates',
}
for name,material in options.items():
    if not a.load_asset(material):raise RuntimeError('Missing floor alternative '+name)
(root/'Saved/Testing/fire-floor-options.json').write_text(json.dumps(options,indent=2))
(backup/'receipt.json').write_text(json.dumps(dict(before_sha256=sha(copy),after_sha256=sha(file),ambient_intensity=.55,ambient_cube=path,protected_assets=len(before)),indent=2)+'\n')
print('ER_FIRE_LIGHTING_SAVED protected_assets='+str(len(before))+' floor_options='+str(len(options)))
