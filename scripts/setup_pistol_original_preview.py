"""Import and open the recovered complete original pistol animation in isolation.

Requires bake_pistol_original_preview.py. Saves only OriginalPreview packages;
existing runtime assets remain hash-protected. Run in the graphical editor.
"""
from pathlib import Path
import hashlib,json
import unreal as ue
root=Path(ue.Paths.project_dir()).resolve()
src=root/'Assets/weapons/SourceAssets/SketchfabWeapons/bd896167e7ca44f19597d3afe6a8d83f'
folder='/Game/EclipseRecoil/Weapons/Sketchfab/Pistol/OriginalPreview'
assets=ue.EditorAssetLibrary;tools=ue.AssetToolsHelpers.get_asset_tools()
def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
protected={p.resolve():sha(p) for sub in ('characters','weapons','maps') for p in (root/'Assets'/sub/'Content').rglob('*.uasset') if '/OriginalPreview/' not in str(p)}
source_hash=sha(src/'original/source/arms@beretta.fbx')
def save(a):
 if not assets.save_loaded_asset(a,False):raise RuntimeError('Cannot save '+a.get_path_name())
def import_asset(file,destination,name,options=None):
 a=ue.load_asset(destination+'/'+name)
 if a:return a
 task=ue.AssetImportTask();task.filename=str(file);task.destination_path=destination;task.destination_name=name;task.automated=True;task.save=True;task.replace_existing=False
 if options:task.options=options
 tools.import_asset_tasks([task]);a=ue.load_asset(destination+'/'+name)
 if not a:raise RuntimeError('Import failed '+str(task.imported_object_paths))
 return a
options=ue.FbxImportUI()
for k,v in dict(automated_import_should_detect_type=False,mesh_type_to_import=ue.FBXImportType.FBXIT_SKELETAL_MESH,import_as_skeletal=True,import_mesh=True,import_animations=True,import_materials=False,import_textures=False,create_physics_asset=False).items():options.set_editor_property(k,v)
mesh=import_asset(src/'original-preview/PistolOriginal.fbx',folder,'SK_PistolOriginal',options)
materials={}
for label in ('arms','beretta'):
 path=folder+'/M_Original_'+label
 mat=ue.load_asset(path) or tools.create_asset('M_Original_'+label,folder,ue.Material,ue.MaterialFactoryNew())
 if assets.get_metadata_tag(mat,'EROriginalPistolPreview')!='1':
  for suffix,prop in [('Color',ue.MaterialProperty.MP_BASE_COLOR),('Normal',ue.MaterialProperty.MP_NORMAL),('Roughness',ue.MaterialProperty.MP_ROUGHNESS),('Metallic',ue.MaterialProperty.MP_METALLIC),('AO',ue.MaterialProperty.MP_AMBIENT_OCCLUSION)]:
   file=src/('original/textures/'+label+suffix+'.png')
   if not file.exists():continue
   tex=import_asset(file,folder+'/Textures','T_Original_'+label+suffix)
   tex.set_editor_property('srgb',suffix=='Color')
   if suffix=='Normal':tex.set_editor_property('compression_settings',ue.TextureCompressionSettings.TC_NORMALMAP)
   save(tex)
   node=ue.MaterialEditingLibrary.create_material_expression(mat,ue.MaterialExpressionTextureSample)
   node.texture=tex
   node.sampler_type=ue.MaterialSamplerType.SAMPLERTYPE_NORMAL if suffix=='Normal' else ue.MaterialSamplerType.SAMPLERTYPE_COLOR if suffix=='Color' else ue.MaterialSamplerType.SAMPLERTYPE_LINEAR_COLOR
   ue.MaterialEditingLibrary.connect_material_property(node,'RGB' if suffix in ('Color','Normal') else 'R',prop)
  assets.set_metadata_tag(mat,'EROriginalPistolPreview','1');ue.MaterialEditingLibrary.recompile_material(mat);save(mat)
 materials[label]=mat
slots=mesh.get_editor_property('materials')
for slot in slots:
 label='arms' if 'arms' in str(slot.material_slot_name).lower() else 'beretta'
 slot.material_interface=materials[label]
mesh.set_editor_property('materials',slots);save(mesh)
clips=[]
for path in assets.list_assets(folder,recursive=False,include_folder=False):
 a=ue.load_asset(path)
 if isinstance(a,ue.AnimSequence):clips.append(a)
full=next((a for a in clips if 'full' in a.get_name().lower()),None)
reload=next((a for a in clips if 'reload' in a.get_name().lower()),None)
if not full or not reload:raise RuntimeError('Missing original clips: '+str([a.get_name() for a in clips]))
if not 2.<reload.get_play_length()<2.5 or not 8.<full.get_play_length()<9.5:raise RuntimeError('Unexpected source durations')
for p,digest in protected.items():
 if sha(p)!=digest:raise RuntimeError('Existing asset changed '+str(p))
if sha(src/'original/source/arms@beretta.fbx')!=source_hash:raise RuntimeError('Original source changed')
report=dict(mesh=mesh.get_path_name(),full=full.get_path_name(),reload=reload.get_path_name(),full_seconds=full.get_play_length(),reload_seconds=reload.get_play_length(),protected_packages=len(protected),source_sha256=source_hash)
(src/'original-preview/unreal-import.json').write_text(json.dumps(report,indent=2)+'\n')
ue.get_editor_subsystem(ue.AssetEditorSubsystem).open_editor_for_assets([reload])
ue.log('ER_PISTOL_ORIGINAL_PREVIEW_READY '+json.dumps(report))
