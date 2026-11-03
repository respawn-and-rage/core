"""Differentiate Warehouse steel roles using only the installed texture library.

Run through UnrealEditor -run=pythonscript. Back up the shared theme library,
create owned material derivatives and update only Warehouse. Original textures,
material parents, recipes and damage catalog remain byte-identical.
"""
from pathlib import Path
import hashlib
import json
import shutil
import unreal as ue

root=Path(ue.Paths.project_dir()).resolve()
a=ue.EditorAssetLibrary;e=ue.MaterialEditingLibrary;t=ue.AssetToolsHelpers.get_asset_tools()
base='/Game/EclipseRecoil/Art/'
folder=base+'FireTraining/WarehouseTheme'
library=a.load_asset(base+'MapTemplates/DA_MapThemes')
if not library:raise RuntimeError('Missing shared theme library')
if a.get_metadata_tag(library,'ERWarehouseSteelRevision')=='1':
    print('ER_WAREHOUSE_STEEL already migrated')
else:
    source_names=[base+'Industrial/SketchfabTextures/M_ER_SF_iron_plates',
                  base+'Industrial/SketchfabTextures/M_ER_SF_steel_irregular',
                  base+'Industrial/Materials/M_ER_FactoryGraySteel',base+'FireTraining/M_ER_WH_Metal']
    protected=[root/('Assets/maps/Content/EclipseRecoil/Art/MapTemplates/DA_'+name+'.uasset')
               for name in ['CentralBuilding','MiddleEasternAlleys','FireTraining','Rooms']]
    protected += [root/'Assets/maps/Content/EclipseRecoil/Art/Destruction/DA_ER_BlockMaterials.uasset']
    protected += [root/('Assets/maps/Content/'+p.removeprefix('/Game/')+'.uasset') for p in source_names]
    sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
    before={str(p):sha(p) for p in protected}
    revision=library.get_editor_property('revision')
    backup=root/('Saved/WarehouseSteelBackup/before-library-revision'+str(revision))
    backup.mkdir(parents=True,exist_ok=True)
    file=root/'Assets/maps/Content/EclipseRecoil/Art/MapTemplates/DA_MapThemes.uasset'
    rollback=backup/file.name
    if not rollback.exists():shutil.copy2(file,rollback)
    if sha(rollback)!=sha(file):raise RuntimeError('Theme rollback mismatch')
    a.make_directory(folder)
    def save(obj):
        if not a.save_loaded_asset(obj,False):raise RuntimeError('Cannot save '+obj.get_path_name())
    def wire(source,pin,target,input_name):
        if not e.connect_material_expressions(source,pin,target,input_name):raise RuntimeError('Cannot connect '+input_name)
    def output(node,prop):
        if not e.connect_material_property(node,'',prop):raise RuntimeError('Cannot connect material output')
    def parent(name,source):
        path=folder+'/'+name
        if a.does_asset_exist(path):
            material=a.load_asset(path)
            if a.get_metadata_tag(material,'ERWarehouseSteelOwned')!='1':raise RuntimeError('Unowned material '+path)
            return material
        material=a.duplicate_asset(source,path)
        if not material:raise RuntimeError('Cannot duplicate '+source)
        color=e.get_material_property_input_node(material,ue.MaterialProperty.MP_BASE_COLOR)
        pin=e.get_material_property_input_node_output_name(material,ue.MaterialProperty.MP_BASE_COLOR)
        if not color:raise RuntimeError('Source material has no color input')
        tint=e.create_material_expression(material,ue.MaterialExpressionVectorParameter,350,0)
        tint.set_editor_property('parameter_name','SteelTint');tint.set_editor_property('default_value',ue.LinearColor(1,1,1,1))
        multiply=e.create_material_expression(material,ue.MaterialExpressionMultiply,600,0)
        wire(color,pin,multiply,'A');wire(tint,'RGB',multiply,'B');output(multiply,ue.MaterialProperty.MP_BASE_COLOR)
        metallic=e.create_material_expression(material,ue.MaterialExpressionScalarParameter,600,160)
        metallic.set_editor_property('parameter_name','MetallicWeight');metallic.set_editor_property('default_value',.45)
        output(metallic,ue.MaterialProperty.MP_METALLIC)
        rough=e.get_material_property_input_node(material,ue.MaterialProperty.MP_ROUGHNESS)
        rough_pin=e.get_material_property_input_node_output_name(material,ue.MaterialProperty.MP_ROUGHNESS)
        floor=e.create_material_expression(material,ue.MaterialExpressionScalarParameter,350,320)
        floor.set_editor_property('parameter_name','MinRoughness');floor.set_editor_property('default_value',.55)
        maximum=e.create_material_expression(material,ue.MaterialExpressionMax,600,320)
        wire(rough,rough_pin,maximum,'A');wire(floor,'',maximum,'B');output(maximum,ue.MaterialProperty.MP_ROUGHNESS)
        a.set_metadata_tag(material,'ERWarehouseSteelOwned','1')
        a.set_metadata_tag(material,'ERWarehouseSteelSource',source)
        e.recompile_material(material);save(material);return material
    def instance(name,parent,tint,tile=None):
        path=folder+'/'+name
        if a.does_asset_exist(path):
            obj=a.load_asset(path)
            if a.get_metadata_tag(obj,'ERWarehouseSteelOwned')!='1':raise RuntimeError('Unowned instance '+path)
            return obj
        obj=t.create_asset(name,folder,ue.MaterialInstanceConstant,ue.MaterialInstanceConstantFactoryNew())
        e.set_material_instance_parent(obj,parent)
        e.set_material_instance_vector_parameter_value(obj,'SteelTint',ue.LinearColor(*tint,1))
        if tile:e.set_material_instance_scalar_parameter_value(obj,'TileSizeCm',tile)
        e.update_material_instance(obj);a.set_metadata_tag(obj,'ERWarehouseSteelOwned','1');save(obj);return obj
    iron=parent('M_ER_WH_RoleIron',source_names[0])
    galvanized=parent('M_ER_WH_RoleGalvanized',source_names[1])
    walls=instance('MI_ER_WH_WallPlates',iron,(1.1,1.35,1.65),300)
    deck=instance('MI_ER_WH_DeckPlates',iron,(.35,.40,.46),200)
    ladders=instance('MI_ER_WH_LadderGalvanized',galvanized,(.85,.9,1),100)
    frame=a.load_asset(source_names[2]);shell=a.load_asset(source_names[3])
    ceiling=instance('MI_ER_WH_RoofSteel',frame,(.45,.50,.58))
    candidate=ue.new_object(ue.ERMapThemeLibrary)
    themes=list(library.get_editor_property('themes'))
    warehouse=next(theme for theme in themes if str(theme.get_editor_property('name'))=='Warehouse')
    materials=list(warehouse.get_editor_property('materials'))
    materials[2]=walls;materials[4]=walls;materials[5]=frame
    for key,value in [('materials',materials),('wall',walls),('ceiling',ceiling),('shell_steel',shell),
                      ('ladder_steel',ladders),('deck_steel',deck),('preserve_cargo_paint',True)]:
        warehouse.set_editor_property(key,value)
    candidate.set_editor_property('themes',themes);candidate.set_editor_property('revision',revision+1)
    for name in ['CentralBuilding','MiddleEasternAlleys','FireTraining','Rooms']:
        saved=a.load_asset(base+'MapTemplates/DA_'+name)
        recipe=ue.new_object(ue.ERMapTemplate)
        for key in ['archetype','central_building','fire_training','rooms','template_id','source_reference','revision','symmetry','shell_wall_mesh','roof_mesh','arena_size','variation','town','pieces','alpha_spawns','omega_spawns','patrol_route']:
            recipe.set_editor_property(key,saved.get_editor_property(key))
        recipe.set_editor_property('theme_library',candidate)
        for seed in [0,3,13,42,73,511,7919,-17,2147483647,-2147483648]:
            if not recipe.validate_layout(seed):raise RuntimeError('Invalid themed recipe '+name+' seed='+str(seed))
    library.set_editor_property('themes',themes);library.set_editor_property('revision',revision+1)
    a.set_metadata_tag(library,'ERWarehouseSteelRevision','1');save(library)
    for path,digest in before.items():
        if sha(Path(path))!=digest:raise RuntimeError('Protected asset changed '+path)
    report=dict(result='PASS',revision=revision+1,before_sha256=sha(rollback),after_sha256=sha(file),
                roles={key:obj.get_path_name() for key,obj in [('shell',shell),('court_walls',walls),('frames',frame),('ladders_and_parapets',ladders),('decks',deck),('roof',ceiling)]},
                protected_sha256=before,validated_layouts=40)
    (backup/'receipt.json').write_text(json.dumps(report,indent=2)+'\n')
    (root/'Saved/Testing/warehouse-steel-assets-report.json').write_text(json.dumps(report,indent=2)+'\n')
    print('ER_WAREHOUSE_STEEL_SAVED '+json.dumps(report['roles']))
