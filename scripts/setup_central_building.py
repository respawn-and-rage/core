"""Create the industrial archetype/map through Unreal; preserve later edits."""
import hashlib,json
from pathlib import Path
import unreal as ue
root=Path(ue.Paths.project_dir());assets=ue.EditorAssetLibrary
folder='/Game/EclipseRecoil/Art/MapTemplates';path=folder+'/DA_CentralBuilding'
template=assets.load_asset(path) if assets.does_asset_exist(path) else None
if not template:
    candidate=ue.new_object(ue.ERMapTemplate)
    candidate.set_editor_property('archetype',ue.ERMapArchetype.CENTRAL_BUILDING)
    candidate.set_editor_property('template_id','industrial.central-building')
    candidate.set_editor_property('source_reference','https://steamcommunity.com/sharedfiles/filedetails/?id=2186934885; Bank/Lake/Safehouse topology, CT=Alpha, T=Omega; original industrial adaptation')
    candidate.set_editor_property('revision',1)
    candidate.set_editor_property('symmetry',ue.ERTemplateSymmetry.HALF_TURN)
    candidate.set_editor_property('arena_size',ue.Vector(8000,10000,1600))
    recipe=candidate.get_editor_property('central_building')
    for prop,name in [('container_mesh','SM_Container01_01'),('tank_mesh','SM_WaterContainer01'),('barrel_mesh','SM_Barrel02')]:
        obj=assets.load_asset('/Game/EclipseRecoil/Art/FactoryEnvironment/Meshes/'+name)
        if not obj:raise RuntimeError('Missing industrial asset '+name)
        recipe.set_editor_property(prop,obj)
    recipe.set_editor_property('deck_surface',assets.load_asset('/Game/EclipseRecoil/Art/ProceduralTextures/M_ER_PBR_rusty_metal_04'))
    candidate.set_editor_property('central_building',recipe)
    seeds=list(range(512))+[7919,-17,2147483647,-2147483648]
    for seed in seeds:
        if not candidate.validate_layout(seed):raise RuntimeError('Invalid central-building layout seed='+str(seed))
    factory=ue.DataAssetFactory();factory.set_editor_property('data_asset_class',ue.ERMapTemplate)
    template=ue.AssetToolsHelpers.get_asset_tools().create_asset('DA_CentralBuilding',folder,ue.ERMapTemplate,factory)
    for prop in ['archetype','template_id','source_reference','revision','symmetry','arena_size','central_building']:
        template.set_editor_property(prop,candidate.get_editor_property(prop))
    assets.set_metadata_tag(template,'ERArchetypeRevision','1')
    if not assets.save_loaded_asset(template):raise RuntimeError('Cannot save central-building recipe')
    print('ER_CENTRAL_SETUP validated_seeds='+str(len(seeds)))
map_path='/Game/EclipseRecoil/Maps/L_CentralBuilding'
levels=ue.get_editor_subsystem(ue.LevelEditorSubsystem)
actors=ue.get_editor_subsystem(ue.EditorActorSubsystem)
if not assets.does_asset_exist(map_path):
    if not levels.new_level(map_path):raise RuntimeError('Cannot create industrial level')
    world=ue.get_editor_subsystem(ue.UnrealEditorSubsystem).get_editor_world()
    world.get_world_settings().set_editor_property('default_game_mode',assets.load_blueprint_class('/Game/EclipseRecoil/Core/BP_ERGameMode'))
    world.get_world_settings().set_editor_property('kill_z',-800)
    def spawn(cls,label,pos):
        obj=actors.spawn_actor_from_class(cls,ue.Vector(*pos),ue.Rotator(pitch=0,yaw=0,roll=0))
        if not obj:raise RuntimeError(label)
        obj.set_actor_label(label);return obj
    generator=spawn(ue.ERProceduralMap,'Industrial_CentralBuilding',(0,0,0))
    config=generator.get_editor_property('layout_config');config.set_editor_property('template_asset',template)
    generator.set_editor_property('layout_config',config);generator.set_editor_property('ceilings',False)
    catalog=assets.load_asset('/Game/EclipseRecoil/Art/Destruction/DA_ER_BlockMaterials')
    if not catalog:raise RuntimeError('Missing cell-material catalog')
    generator.set_editor_property('material_catalog',catalog)
    generator.set_editor_property('floor_surface',assets.load_asset('/Game/EclipseRecoil/Art/ProceduralTextures/M_ER_PBR_asphalt_02'))
    generator.set_editor_property('explosion_effect_class',assets.load_blueprint_class('/Game/EclipseRecoil/Effects/Grenade/BP_ERGrenadeExplosion'))
    # Runtime generation repositions these from the validated descriptor before
    # pawns spawn. The saved placeholders are not a second layout definition.
    for team in ['ALPHA','OMEGA']:
        for i in range(10):
            start=spawn(ue.ERTeamPlayerStart,team+'_Start_'+str(i),(0,0,100))
            start.set_editor_property('team',getattr(ue.ERTeam,team))
    for i in range(12):
        point=spawn(ue.ERBotPatrolPoint,'Industrial_Patrol_'+str(i),(0,0,100));point.set_editor_property('route_index',i)
    sun=spawn(ue.DirectionalLight,'Industrial_Sun',(0,0,2500))
    sun.set_actor_rotation(ue.Rotator(pitch=-55,yaw=-35,roll=0),False)
    sun.light_component.set_editor_property('intensity',3.5)
    sky=spawn(ue.SkyLight,'Industrial_Sky',(0,0,2200));sky.light_component.set_editor_property('intensity',1.1)
    spawn(ue.SkyAtmosphere,'Industrial_Atmosphere',(0,0,0))
    if not levels.save_current_level():raise RuntimeError('Cannot save industrial level')
if not levels.load_level(map_path):raise RuntimeError('Cannot open industrial lighting')
world=ue.get_editor_subsystem(ue.UnrealEditorSubsystem).get_editor_world()
if assets.get_metadata_tag(world,'ERIndustrialLightingRevision')!='1':
    for actor in actors.get_all_level_actors():
        if isinstance(actor,ue.SkyLight):
            actor.light_component.set_mobility(ue.ComponentMobility.MOVABLE)
            actor.light_component.set_editor_property('real_time_capture',True)
            actor.light_component.set_editor_property('lower_hemisphere_is_black',False)
            actor.light_component.set_editor_property('intensity',1.5)
        elif isinstance(actor,ue.DirectionalLight):
            actor.light_component.set_mobility(ue.ComponentMobility.MOVABLE)
            actor.light_component.set_editor_property('atmosphere_sun_light',True)
    assets.set_metadata_tag(world,'ERIndustrialLightingRevision','1')
    if not levels.save_current_level():raise RuntimeError('Cannot save industrial lighting')
print('ER_CENTRAL_SETUP_COMPLETE')
