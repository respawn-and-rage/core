"""Unreal-only import and creation of the Fire Training warehouse template.

Run after stage_fire_training_assets.py and bake_fire_training_kit.py:
UnrealEditor <project> -run=pythonscript -script=<abs path> -nullrhi -nosound -unattended -NoSplash

Imports the prepared kit at unit scale, builds owned materials from the
warehouse textures, then creates DA_FireTraining and L_FireTraining when absent.
Completed assets are preserved; other maps and the shared catalog are verified
unchanged. The original archive stays in the private maps mount.
"""
import hashlib
import json
from pathlib import Path
import unreal as ue

project=Path(ue.Paths.project_dir()).resolve()
source=project/'Assets/maps/SourceAssets/FireTraining'
kit=source/'prepared_kit'
folder='/Game/EclipseRecoil/Art/FireTraining'
assets=ue.EditorAssetLibrary;edit=ue.MaterialEditingLibrary;tools=ue.AssetToolsHelpers.get_asset_tools()
protected=[project/'Assets/maps/Content/EclipseRecoil'/p for p in [
    'Maps/L_Depot.umap','Maps/L_CentralBuilding.umap','Maps/L_MiddleEasternAlleys.umap',
    'Art/MapTemplates/DA_CentralBuilding.uasset','Art/MapTemplates/DA_MiddleEasternAlleys.uasset',
    'Art/Destruction/DA_ER_BlockMaterials.uasset']]
digest=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
before={str(p):digest(p) for p in protected if p.exists()}
manifest=json.loads((kit/'manifest.json').read_text())
textures=source/'original/textures'

def load(path):
    obj=assets.load_asset(path)
    if not obj:raise RuntimeError('Missing '+path)
    return obj

def save(obj):
    if not assets.save_loaded_asset(obj,False):raise RuntimeError('Cannot save '+obj.get_path_name())

def texture(path,name,normal=False,linear=False):
    target=folder+'/Textures/'+name
    if assets.does_asset_exist(target):return load(target)
    task=ue.AssetImportTask();task.filename=str(path);task.destination_path=folder+'/Textures';task.destination_name=name
    task.automated=True;task.factory=ue.TextureFactory();tools.import_asset_tasks([task])
    obj=load(target);obj.set_editor_property('srgb',not(normal or linear))
    obj.set_editor_property('compression_settings',ue.TextureCompressionSettings.TC_NORMALMAP if normal else ue.TextureCompressionSettings.TC_MASKS if linear else ue.TextureCompressionSettings.TC_DEFAULT)
    assets.set_metadata_tag(obj,'ERSourceSHA256',digest(path));save(obj);return obj

def node(mat,cls):return edit.create_material_expression(mat,cls,0,0)
def wire(a,pin,b,target):
    if not edit.connect_material_expressions(a,pin,b,target):raise RuntimeError('Material connection '+target)
def output(a,pin,prop):
    if not edit.connect_material_property(a,pin,prop):raise RuntimeError('Material output '+str(prop))
def scalar(mat,value):
    n=node(mat,ue.MaterialExpressionConstant);n.set_editor_property('r',value);return n
def color(mat,rgb):
    n=node(mat,ue.MaterialExpressionConstant3Vector);n.set_editor_property('constant',ue.LinearColor(*rgb,1));return n
def custom(mat,code,inputs,kind):
    n=node(mat,ue.MaterialExpressionCustom);n.set_editor_property('code',code);n.set_editor_property('output_type',kind)
    fields=[]
    for name in inputs:
        field=ue.CustomInput();field.set_editor_property('input_name',name);fields.append(field)
    n.set_editor_property('inputs',fields)
    for name,value in inputs.items():wire(value,'',n,name)
    return n
def material(name):
    target=folder+'/'+name
    if assets.does_asset_exist(target):return load(target),False
    m=tools.create_asset(name,folder,ue.Material,ue.MaterialFactoryNew());m.set_editor_property('used_with_instanced_static_meshes',True);return m,True
def sample(mat,tex,kind):
    s=node(mat,ue.MaterialExpressionTextureSample);s.set_editor_property('texture',tex);s.set_editor_property('sampler_type',kind);return s

# Corrugated sheet: source normals, a lighter grey and partial metalness so the
# game's no-reflection profile still reads it as steel rather than black.
metal,new=material('M_ER_WH_Metal')
if new:
    normal=sample(metal,texture(textures/'Warehouse_normals.png','T_ER_WH_SheetNormal',normal=True),ue.MaterialSamplerType.SAMPLERTYPE_NORMAL)
    output(normal,'RGB',ue.MaterialProperty.MP_NORMAL)
    output(color(metal,(.30,.30,.31)),'',ue.MaterialProperty.MP_BASE_COLOR)
    output(scalar(metal,.35),'',ue.MaterialProperty.MP_METALLIC);output(scalar(metal,.6),'',ue.MaterialProperty.MP_ROUGHNESS)
    edit.recompile_material(metal);save(metal)
lamp,new=material('M_ER_WH_LampEmissive')
if new:
    output(color(lamp,(.8,.8,.8)),'',ue.MaterialProperty.MP_BASE_COLOR)
    output(color(lamp,(6,5.6,4.8)),'',ue.MaterialProperty.MP_EMISSIVE_COLOR)
    edit.recompile_material(lamp);save(lamp)
# Wet concrete floor projected in world space (4m tiles) on the ground slab.
ground,new=material('M_ER_WH_WetConcrete')
if new:
    ground.set_editor_property('tangent_space_normal',False)
    pos=node(ground,ue.MaterialExpressionWorldPosition)
    uv=custom(ground,'return float2(P.x,-P.y)/400.0;',{'P':pos},ue.CustomMaterialOutputType.CMOT_FLOAT2)
    parts={}
    for role,file,kind,normal_map,linear in [('Color','WetConcrete_Base_color.png',ue.MaterialSamplerType.SAMPLERTYPE_COLOR,False,False),
                                             ('Normal','WetConcrete_Normal_DirectX.png',ue.MaterialSamplerType.SAMPLERTYPE_NORMAL,True,False),
                                             ('Roughness','WetConcrete_Roughness@channels=G.png',ue.MaterialSamplerType.SAMPLERTYPE_MASKS,False,True)]:
        s=sample(ground,texture(textures/file,'T_ER_WH_Concrete'+role,normal_map,linear),kind);wire(uv,'',s,'UVs');parts[role]=s
    output(parts['Color'],'RGB',ue.MaterialProperty.MP_BASE_COLOR)
    output(custom(ground,'return normalize(float3(A.xy*float2(0.55,-0.55),1.0));',{'A':parts['Normal']},ue.CustomMaterialOutputType.CMOT_FLOAT3),'',ue.MaterialProperty.MP_NORMAL)
    # The source map is a glossy wet finish; keep it but avoid a mirror floor.
    output(custom(ground,'return saturate(0.25+A.r*2.0);',{'A':parts['Roughness']},ue.CustomMaterialOutputType.CMOT_FLOAT1),'',ue.MaterialProperty.MP_ROUGHNESS)
    edit.recompile_material(ground);save(ground)
for m in (metal,lamp,ground):assets.set_metadata_tag(m,'ERSourceArchive',json.loads((source/'provenance.json').read_text())['archive_sha256']);save(m)

def mesh(entry):
    name=entry['name'];path=kit/(name+'.fbx');target=folder+'/Meshes/'+name
    if digest(path)!=entry['sha256']:raise RuntimeError('Kit file changed '+name)
    if assets.does_asset_exist(target):
        obj=load(target)
        if assets.get_metadata_tag(obj,'ERSourceSHA256')!=entry['sha256']:raise RuntimeError('Imported module differs from kit '+name)
        return obj
    task=ue.AssetImportTask();task.filename=str(path);task.destination_path=folder+'/Meshes';task.destination_name=name
    task.automated=True;task.save=False
    opts=ue.FbxImportUI();opts.set_editor_property('import_as_skeletal',False);opts.set_editor_property('import_materials',False);opts.set_editor_property('import_textures',False)
    opts.set_editor_property('mesh_type_to_import',ue.FBXImportType.FBXIT_STATIC_MESH)
    data=opts.static_mesh_import_data
    data.set_editor_property('combine_meshes',True);data.set_editor_property('generate_lightmap_u_vs',False)
    data.set_editor_property('auto_generate_collision',False)
    data.set_editor_property('normal_import_method',ue.FBXNormalImportMethod.FBXNIM_IMPORT_NORMALS)
    task.options=opts;tools.import_asset_tasks([task]);obj=load(target)
    for slot,info in enumerate(obj.get_editor_property('static_materials')):
        obj.set_material(slot,lamp if 'Emissive' in str(info.get_editor_property('material_slot_name')) else metal)
    assets.set_metadata_tag(obj,'ERSourceSHA256',entry['sha256']);assets.set_metadata_tag(obj,'ERKitRole',entry['role'])
    # Unit-scale kit: the imported bounds must equal the measured Blender bounds.
    box=obj.get_bounding_box();lo=box.min;hi=box.max
    want_lo=[x*100 for x in entry['bounds_min_m']];want_hi=[x*100 for x in entry['bounds_max_m']]
    got=[(lo.x,hi.x),(lo.y,hi.y),(lo.z,hi.z)]
    for axis,(a,b) in enumerate(got):
        same=abs(a-want_lo[axis])<.6 and abs(b-want_hi[axis])<.6
        flipped=axis==1 and abs(a+want_hi[axis])<.6 and abs(b+want_lo[axis])<.6
        if not(same or flipped):raise RuntimeError(f'{name} axis {axis} bounds {a:.2f}..{b:.2f} expected {want_lo[axis]:.2f}..{want_hi[axis]:.2f}')
    if entry['role'] not in ('RoofLamps','SideWallLamps','EndPortal') and not ue.ERMapTemplate.prepare_warehouse_collision(obj):
        raise RuntimeError('Cannot prepare collision '+name)
    save(obj);return obj

modules={e['name']:mesh(e) for e in manifest['modules']}
shared=dict(side_wall_bay='SM_ER_WH_SideWallBay',side_wall_lamps='SM_ER_WH_SideWallLamps',post='SM_ER_WH_Post',end_portal='SM_ER_WH_EndPortal')
central=load('/Game/EclipseRecoil/Art/MapTemplates/DA_CentralBuilding').get_editor_property('central_building')
template_path='/Game/EclipseRecoil/Art/MapTemplates/DA_FireTraining'
kit_sha=digest(kit/'manifest.json')
if assets.does_asset_exist(template_path):
    template=load(template_path)
    if assets.get_metadata_tag(template,'ERFireTrainingKitSHA256')!=kit_sha:
        raise RuntimeError('Completed Fire Training kit changed; use an explicit versioned migration')
else:
    candidate=ue.new_object(ue.ERMapTemplate)
    for key,value in [('template_id','industrial.fire-training'),('revision',1),
                      ('source_reference','Killhouse-style training warehouse (owner references: CoD4 Killhouse, Steam Workshop 526448928); original seeded layout on the supplied warehouse kit'),
                      ('archetype',ue.ERMapArchetype.FIRE_TRAINING),('symmetry',ue.ERTemplateSymmetry.HALF_TURN),('arena_size',ue.Vector(2460,5540,1120))]:
        candidate.set_editor_property(key,value)
    recipe=ue.ERFireTrainingRecipe()
    widths=[]
    for width in manifest['widths_cm']:
        k=ue.ERWarehouseWidthKit();k.set_editor_property('width_cm',width)
        for key,role in [('roof_bay','RoofBay'),('roof_arch','RoofArch'),('roof_lamps','RoofLamps'),('end_ground','EndGround'),('end_band','EndBand'),('end_crown','EndCrown')]:
            k.set_editor_property(key,modules[f'SM_ER_WH_{role}_W{width}'])
        widths.append(k)
    recipe.set_editor_property('widths',widths)
    for key,name in shared.items():recipe.set_editor_property(key,modules[name])
    recipe.set_editor_property('ground_surface',ground)
    recipe.set_editor_property('parapet_module',central.get_editor_property('fence_module'))
    for key in ['steel_surface','deck_surface','wood_surfaces','concrete_surfaces','container_surfaces']:
        recipe.set_editor_property(key,central.get_editor_property(key))
    candidate.set_editor_property('fire_training',recipe)
    for seed in list(range(512))+[7919,-17,2147483647,-2147483648]:
        if not candidate.validate_layout(seed):raise RuntimeError('Invalid Fire Training seed '+str(seed))
    factory=ue.DataAssetFactory();factory.set_editor_property('data_asset_class',ue.ERMapTemplate)
    template=tools.create_asset('DA_FireTraining','/Game/EclipseRecoil/Art/MapTemplates',ue.ERMapTemplate,factory)
    for key in ['template_id','revision','source_reference','archetype','symmetry','arena_size','fire_training']:template.set_editor_property(key,candidate.get_editor_property(key))
    assets.set_metadata_tag(template,'ERFireTrainingKitSHA256',kit_sha);save(template)
    print('ER_FIRE_TRAINING_TEMPLATE_SAVED seeds=516')

map_path='/Game/EclipseRecoil/Maps/L_FireTraining'
levels=ue.get_editor_subsystem(ue.LevelEditorSubsystem);actors=ue.get_editor_subsystem(ue.EditorActorSubsystem)
if not assets.does_asset_exist(map_path):
    if not levels.new_level(map_path):raise RuntimeError('Cannot create Fire Training level')
    world=ue.get_editor_subsystem(ue.UnrealEditorSubsystem).get_editor_world()
    world.get_world_settings().set_editor_property('default_game_mode',assets.load_blueprint_class('/Game/EclipseRecoil/Core/BP_ERGameMode'))
    world.get_world_settings().set_editor_property('kill_z',-800)
    def spawn(cls,label,pos):
        a=actors.spawn_actor_from_class(cls,ue.Vector(*pos),ue.Rotator(pitch=0,yaw=0,roll=0))
        if not a:raise RuntimeError(label)
        a.set_actor_label(label);return a
    generator=spawn(ue.ERProceduralMap,'FireTraining_Warehouse',(0,0,0))
    config=generator.get_editor_property('layout_config');config.set_editor_property('template_asset',template);generator.set_editor_property('layout_config',config)
    # The hidden ceiling closes the volume above the arched roof.
    generator.set_editor_property('ceilings',True);generator.set_editor_property('floor_surface',ground)
    generator.set_editor_property('material_catalog',load('/Game/EclipseRecoil/Art/Destruction/DA_ER_BlockMaterials'))
    generator.set_editor_property('explosion_effect_class',assets.load_blueprint_class('/Game/EclipseRecoil/Effects/Grenade/BP_ERGrenadeExplosion'))
    for team in ['ALPHA','OMEGA']:
        for i in range(10):spawn(ue.ERTeamPlayerStart,team+'_Start_'+str(i),(0,0,100)).set_editor_property('team',getattr(ue.ERTeam,team))
    for i in range(8):spawn(ue.ERBotPatrolPoint,'FireTraining_Patrol_'+str(i),(0,0,100)).set_editor_property('route_index',i)
    sun=spawn(ue.DirectionalLight,'FireTraining_Sun',(0,0,2500));sun.set_actor_rotation(ue.Rotator(pitch=-50,yaw=-30,roll=0),False)
    sun.light_component.set_mobility(ue.ComponentMobility.MOVABLE);sun.light_component.set_editor_property('intensity',3.5);sun.light_component.set_editor_property('atmosphere_sun_light',True)
    sky=spawn(ue.SkyLight,'FireTraining_Sky',(0,0,2200));sky.light_component.set_mobility(ue.ComponentMobility.MOVABLE)
    sky.light_component.set_editor_property('real_time_capture',True);sky.light_component.set_editor_property('lower_hemisphere_is_black',False);sky.light_component.set_editor_property('intensity',1.5)
    spawn(ue.SkyAtmosphere,'FireTraining_Atmosphere',(0,0,0))
    if not levels.save_current_level():raise RuntimeError('Cannot save Fire Training level')
credits=project/'Branding/Credits.txt';text=credits.read_text();heading='FIRE TRAINING — WAREHOUSE KIT'
if heading not in text:
    text=text.replace('THANK YOU\n',heading+'\n'+(source/'CREDITS.txt').read_text()+'\nTHANK YOU\n');credits.write_text(text)
for path,sha in before.items():
    if digest(Path(path))!=sha:raise RuntimeError('Protected asset changed '+path)
(source/'import-receipt.json').write_text(json.dumps(dict(template=template_path,map=map_path,kit_manifest_sha256=kit_sha,
    modules={k:v.get_path_name() for k,v in modules.items()},materials=[m.get_path_name() for m in (metal,lamp,ground)],preserved_sha256=before),indent=2)+'\n')
print('ER_FIRE_TRAINING_SETUP_COMPLETE')
