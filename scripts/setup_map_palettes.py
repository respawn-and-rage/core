"""Bind each production template to its own palette, where every finish shows its physical material.

Run through UnrealEditor -run=pythonscript after compiling protocol 19. Two owned
derivatives give Alleys sandstone and pale brick. DA_MapThemes stays on disk as the
input of older migrations. Damage catalog, levels and source materials stay untouched.
"""
from pathlib import Path
import hashlib,json,zipfile
import unreal as ue
root=Path(ue.Paths.project_dir()).resolve();private=root/'Assets/maps';a=ue.EditorAssetLibrary;e=ue.MaterialEditingLibrary
tools=ue.AssetToolsHelpers.get_asset_tools()
art='/Game/EclipseRecoil/Art/';base=art+'MapTemplates/';alley=art+'MiddleEasternAlleys/'
names=['CentralBuilding','MiddleEasternAlleys','FireTraining','Rooms','Village']
sf=art+'Industrial/SketchfabTextures/M_ER_SF_';pbr=art+'ProceduralTextures/M_ER_PBR_';wh=art+'Industrial/SharedSteel/'
glass=art+'ProceduralTextures/M_ER_Glass';village=art+'Village/M_ER_Jungle_'
ladder=wh+'MI_ER_IndustrialLadderGalvanized';deck=wh+'MI_ER_IndustrialDeckPlates';dry=sf+'concrete_seamless'
sand=alley+'MI_ER_AlleySandstone';pale=alley+'MI_ER_AlleyLightBrick'
plaster=pbr+'Plaster001';brick=pbr+'red_brick_03';stone=pbr+'stone_wall_02';rust=pbr+'rusty_metal_04'
concrete=sf+'concrete_seamless';cast=pbr+'concrete_wall_006';wood=sf+'wood_05'
# Order follows EERBlockMaterial: Drywall, Brick, Concrete, Wood, Stone, Metal, Glass.
palettes=dict(
    CentralBuilding=dict(name='Urban',materials=[plaster,brick,concrete,wood,stone,rust,glass],wall=concrete,floor=deck,ceiling=deck,grounds=[sf+'asphalt_11',sf+'grass_02',sf+'snow_01'],shell=wh+'M_ER_IndustrialShellSteel'),
    MiddleEasternAlleys=dict(name='Stone',materials=[alley+'M_ER_AlleyPlaster',pale,concrete,wood,sand,rust,glass],wall=sand,floor=alley+'M_ER_AlleyRoof',ceiling=alley+'M_ER_AlleyRoof',grounds=[alley+'M_ER_ThreeSands'],shell=wh+'M_ER_IndustrialShellSteel'),
    FireTraining=dict(name='Training Hall',materials=[plaster,brick,concrete,wood,stone,rust,glass],wall=concrete,floor=dry,ceiling=art+'FireTraining/Procedural/M_ER_FireTraining_CeilingSteel',grounds=[dry],shell=wh+'M_ER_IndustrialShellSteel'),
    Rooms=dict(name='Tunnels',materials=[plaster,brick,cast,wood,stone,rust,glass],wall=cast,floor=dry,ceiling=cast,grounds=[dry],shell=wh+'M_ER_IndustrialShellSteel'),
    Village=dict(name='Jungle',materials=[plaster,brick,concrete,village+'Timber',stone,village+'Corrugated',glass],wall=village+'Timber',floor=village+'Timber',ceiling=village+'Corrugated',grounds=[village+'Ground'],shell=art+'Village/Topology/M_Village_GraySheet'),
)
def disk(path):return private/('Content/'+path.removeprefix('/Game/')+'.uasset')
def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def load(path):
    obj=a.load_asset(path)
    if not obj:raise RuntimeError('Missing '+path)
    return obj
def save(obj):
    if not a.save_loaded_asset(obj,False):raise RuntimeError('Cannot save '+obj.get_path_name())
recipes={n:load(base+'DA_'+n) for n in names}
protected=[disk(art+'Destruction/DA_ER_BlockMaterials'),disk(stone),disk(brick)]+list((private/'Content/EclipseRecoil/Maps').glob('*.umap'))
before={str(p):sha(p) for p in protected}
backup=root/'Saved/MapPalettesBackup/before-map-palettes.zip';backup.parent.mkdir(parents=True,exist_ok=True)
files=[disk(base+'DA_'+n) for n in names]+[disk(base+'DA_MapThemes')]
if not backup.exists():
    with zipfile.ZipFile(backup,'w',zipfile.ZIP_DEFLATED) as z:
        for p in files:z.write(p,p.name)
        z.writestr('hashes.json',json.dumps({p.name:sha(p) for p in files}))
with zipfile.ZipFile(backup) as z:
    for name,digest in json.loads(z.read('hashes.json')).items():
        if hashlib.sha256(z.read(name)).hexdigest()!=digest:raise RuntimeError('Invalid palette rollback')
def parent(path,source):
    if a.does_asset_exist(path):
        material=load(path)
        if a.get_metadata_tag(material,'ERMapPaletteOwned')!='1':raise RuntimeError('Unowned material '+path)
        return material
    material=a.duplicate_asset(source,path)
    if not material:raise RuntimeError('Cannot duplicate '+source)
    color=e.get_material_property_input_node(material,ue.MaterialProperty.MP_BASE_COLOR)
    pin=e.get_material_property_input_node_output_name(material,ue.MaterialProperty.MP_BASE_COLOR)
    if not color:raise RuntimeError('Source material has no color input')
    grey=e.create_material_expression(material,ue.MaterialExpressionDesaturation,350,-120)
    amount=e.create_material_expression(material,ue.MaterialExpressionScalarParameter,100,-40)
    amount.set_editor_property('parameter_name','Desaturate')
    tint=e.create_material_expression(material,ue.MaterialExpressionVectorParameter,350,60)
    tint.set_editor_property('parameter_name','FinishTint');tint.set_editor_property('default_value',ue.LinearColor(1,1,1,1))
    multiply=e.create_material_expression(material,ue.MaterialExpressionMultiply,600,0)
    for source_node,output,target,target_input in [(color,pin,grey,''),(amount,'',grey,'Fraction'),(grey,'',multiply,'A'),(tint,'RGB',multiply,'B')]:
        if not e.connect_material_expressions(source_node,output,target,target_input):raise RuntimeError('Cannot connect '+path)
    if not e.connect_material_property(multiply,'',ue.MaterialProperty.MP_BASE_COLOR):raise RuntimeError('Cannot connect output '+path)
    a.set_metadata_tag(material,'ERMapPaletteOwned','1');a.set_metadata_tag(material,'ERMapPaletteSource',source)
    e.recompile_material(material);save(material);return material
def instance(path,material,desaturate,tint):
    folder,name=path.rsplit('/',1)
    if a.does_asset_exist(path):
        obj=load(path)
        if a.get_metadata_tag(obj,'ERMapPaletteOwned')!='1':raise RuntimeError('Unowned instance '+path)
    else:
        obj=tools.create_asset(name,folder,ue.MaterialInstanceConstant,ue.MaterialInstanceConstantFactoryNew())
        e.set_material_instance_parent(obj,material);a.set_metadata_tag(obj,'ERMapPaletteOwned','1')
    e.set_material_instance_scalar_parameter_value(obj,'Desaturate',desaturate)
    e.set_material_instance_vector_parameter_value(obj,'FinishTint',ue.LinearColor(*tint,1))
    e.update_material_instance(obj);save(obj);return obj
instance(sand,parent(alley+'M_ER_AlleySandstone',stone),.15,(1.25,1.08,.82))
instance(pale,parent(alley+'M_ER_AlleyLightBrick',brick),.65,(2.4,2.05,1.45))
report={}
for name in names:
    recipe=recipes[name];spec=palettes[name];path=base+'DA_Theme_'+name
    if a.does_asset_exist(path):library=load(path)
    else:
        factory=ue.DataAssetFactory();factory.set_editor_property('data_asset_class',ue.ERMapThemeLibrary)
        library=tools.create_asset('DA_Theme_'+name,base.rstrip('/'),ue.ERMapThemeLibrary,factory)
    # Retain the explicit ceiling/wall finish migration when older setup is rerun.
    if name=='FireTraining' and a.get_metadata_tag(library,'ERFireTrainingFinishesRevision')=='1':
        theme=list(library.get_editor_property('themes'))[0]
    else:
        theme=ue.ERMapThemePalette()
        for key,value in dict(name=spec['name'],materials=[load(p) for p in spec['materials']],wall=load(spec['wall']),floor=load(spec['floor']),ceiling=load(spec['ceiling']),grounds=[load(p) for p in spec['grounds']],shell_steel=load(spec['shell']),ladder_steel=load(ladder),deck_steel=load(deck),preserve_cargo_paint=True).items():
            theme.set_editor_property(key,value)
    library.set_editor_property('themes',[theme]);save(library)
    recipe.set_editor_property('theme_library',library)
    if a.get_metadata_tag(recipe,'ERMapPaletteRevision')!='1':
        recipe.set_editor_property('revision',recipe.get_editor_property('revision')+1)
        a.set_metadata_tag(recipe,'ERMapPaletteRevision','1')
    for seed in [*range(512),7919,-17,2147483647,-2147483648]:
        if not recipe.validate_layout(seed):raise RuntimeError('Rejected '+name+' seed='+str(seed))
    save(recipe);report[name]=dict(palette=spec['name'],library_sha256=sha(disk(path)),recipe_sha256=sha(disk(base+'DA_'+name)))
for path,digest in before.items():
    if sha(Path(path))!=digest:raise RuntimeError('Protected asset changed '+path)
receipt=dict(result='PASS',validated_seeds_per_recipe=516,templates=report,backup=str(backup),protected_sha256=before)
(private/'SourceAssets/MapTemplates/map-palettes.json').write_text(json.dumps(receipt,indent=2)+'\n')
print('ER_MAP_PALETTES_SETUP_COMPLETE templates=5 seeds=516')
