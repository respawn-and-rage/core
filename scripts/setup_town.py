"""Create the owned Town recipe, palette and level using existing urban materials.

Run in Unreal Python after compiling. Existing packages are hash protected; owned
outputs have a verified rollback. No third-party geometry is imported.
"""
import hashlib
import json
import zipfile
from pathlib import Path
import unreal as ue

root = Path(ue.Paths.project_dir()).resolve()
content = root/'Assets/maps/Content/EclipseRecoil'
assets = ue.EditorAssetLibrary
tools = ue.AssetToolsHelpers.get_asset_tools()
base = '/Game/EclipseRecoil/Art/MapTemplates/'
owned = {'DA_Town', 'DA_Theme_Town', 'L_Town'}
sha = lambda p: hashlib.sha256(p.read_bytes()).hexdigest()
protected = [p for p in content.rglob('*') if p.suffix in {'.uasset', '.umap'} and p.stem not in owned]
before = {str(p): sha(p) for p in protected}
backup = root/'Saved/TownBackup/before-town.zip'
backup.parent.mkdir(parents=True, exist_ok=True)
if not backup.exists():
    with zipfile.ZipFile(backup, 'w', zipfile.ZIP_DEFLATED) as archive:
        outputs = [p for p in content.rglob('*') if p.stem in owned and p.is_file()]
        for p in outputs:
            archive.write(p, str(p.relative_to(content)))
        archive.writestr('hashes.json', json.dumps({str(p.relative_to(content)): sha(p) for p in outputs}))
with zipfile.ZipFile(backup) as archive:
    for name, digest in json.loads(archive.read('hashes.json')).items():
        if hashlib.sha256(archive.read(name)).hexdigest() != digest:
            raise RuntimeError('Invalid Town rollback '+name)

def load(path):
    obj = assets.load_asset(path)
    if not obj:
        raise RuntimeError('Missing '+path)
    return obj

def save(obj):
    if not assets.save_loaded_asset(obj, False):
        raise RuntimeError('Cannot save '+obj.get_path_name())

def data(name, cls):
    if assets.does_asset_exist(base+name):
        obj = load(base+name)
        if assets.get_metadata_tag(obj, 'ERTownOwned') != '1':
            raise RuntimeError('Unowned Town output '+name)
        return obj
    factory = ue.DataAssetFactory()
    factory.set_editor_property('data_asset_class', cls)
    obj = tools.create_asset(name, base.rstrip('/'), cls, factory)
    assets.set_metadata_tag(obj, 'ERTownOwned', '1')
    return obj

# Asphalt is already imported from the owner's RR collection. Keep the other
# six physical finishes and their existing provenance; no new downloads.
source = load(base+'DA_Theme_CentralBuilding')
palette = source.get_editor_property('themes')[0]
palette.set_editor_property('name', 'Town')
art = '/Game/EclipseRecoil/Art/'
concrete = load(art+'Industrial/SketchfabTextures/M_ER_SF_concrete_seamless')
materials = list(palette.get_editor_property('materials'))
materials[1] = load(art+'Industrial/SketchfabTextures/M_ER_SF_brick_tiled')
palette.set_editor_property('materials', materials)
palette.set_editor_property('floor', concrete)
palette.set_editor_property('ceiling', concrete)
palette.set_editor_property('grounds', [load(art+'Industrial/SketchfabTextures/M_ER_SF_asphalt_11')])
library = data('DA_Theme_Town', ue.ERMapThemeLibrary)
library.set_editor_property('themes', [palette])
save(library)
candidate = ue.new_object(ue.ERMapTemplate)
properties = dict(template_id='town.urban', revision=1, archetype=ue.ERMapArchetype.URBAN_TOWN,
                  symmetry=ue.ERTemplateSymmetry.HALF_TURN, arena_size=ue.Vector(5000, 6000, 1400),
                  theme_library=library, source_reference='Original urban geometry inspired by the owner-supplied Chinatown overhead reference; no Counter-Strike assets imported')
for key, value in properties.items():
    candidate.set_editor_property(key, value)
for seed in [*range(512), 7919, -17, 2147483647, -2147483648]:
    if not candidate.validate_layout(seed):
        raise RuntimeError('Town rejected seed='+str(seed))
template = data('DA_Town', ue.ERMapTemplate)
for key, value in properties.items():
    template.set_editor_property(key, value)
save(template)
levels = ue.get_editor_subsystem(ue.LevelEditorSubsystem)
actors = ue.get_editor_subsystem(ue.EditorActorSubsystem)
map_path = '/Game/EclipseRecoil/Maps/L_Town'
if not assets.does_asset_exist(map_path):
    if not levels.new_level(map_path):
        raise RuntimeError('Cannot create Town level')
    world = ue.get_editor_subsystem(ue.UnrealEditorSubsystem).get_editor_world()
    world.get_world_settings().set_editor_property('default_game_mode', assets.load_blueprint_class('/Game/EclipseRecoil/Core/BP_ERGameMode'))
    world.get_world_settings().set_editor_property('kill_z', -800)
    def spawn(cls, label, xyz=(0, 0, 100)):
        obj = actors.spawn_actor_from_class(cls, ue.Vector(*xyz), ue.Rotator(pitch=0, yaw=0, roll=0))
        obj.set_actor_label(label)
        return obj
    generator = spawn(ue.ERProceduralMap, 'Town_Generator', (0, 0, 0))
    config = generator.get_editor_property('layout_config')
    config.set_editor_property('template_asset', template)
    generator.set_editor_property('layout_config', config)
    generator.set_editor_property('ceilings', False)
    generator.set_editor_property('material_catalog', load('/Game/EclipseRecoil/Art/Destruction/DA_ER_BlockMaterials'))
    generator.set_editor_property('explosion_effect_class', assets.load_blueprint_class('/Game/EclipseRecoil/Effects/Grenade/BP_ERGrenadeExplosion'))
    for team in ['ALPHA', 'OMEGA']:
        for index in range(10):
            spawn(ue.ERTeamPlayerStart, team+'_Start_'+str(index)).set_editor_property('team', getattr(ue.ERTeam, team))
    for index in range(12):
        spawn(ue.ERBotPatrolPoint, 'Town_Patrol_'+str(index)).set_editor_property('route_index', index)
    sun = spawn(ue.DirectionalLight, 'Town_Sun', (0, 0, 2500))
    sun.set_actor_rotation(ue.Rotator(pitch=-55, yaw=-35, roll=0), False)
    sun.light_component.set_editor_property('intensity', 4)
    sky = spawn(ue.SkyLight, 'Town_Sky', (0, 0, 2200))
    sky.light_component.set_mobility(ue.ComponentMobility.MOVABLE)
    sky.light_component.set_editor_property('intensity', 1.2)
    spawn(ue.SkyAtmosphere, 'Town_Atmosphere', (0, 0, 0))
    if not levels.save_current_level():
        raise RuntimeError('Cannot save Town level')
if not ue.EditorLoadingAndSavingUtils.load_map(map_path):
    raise RuntimeError('Cannot load owned Town level')
world = ue.get_editor_subsystem(ue.UnrealEditorSubsystem).get_editor_world()
for sun in ue.GameplayStatics.get_all_actors_of_class(world, ue.DirectionalLight):
    sun.light_component.set_mobility(ue.ComponentMobility.MOVABLE)
    sun.light_component.set_editor_property('atmosphere_sun_light', True)
    sun.light_component.set_editor_property('light_color', ue.Color(r=255, g=246, b=232, a=255))
for sky in ue.GameplayStatics.get_all_actors_of_class(world, ue.SkyLight):
    sky.light_component.set_editor_property('real_time_capture', True)
    sky.light_component.set_editor_property('intensity', 1.7)
    sky.light_component.set_editor_property('light_color', ue.Color(r=255, g=244, b=234, a=255))
    sky.light_component.set_editor_property('lower_hemisphere_is_black', True)
    sky.light_component.set_editor_property('lower_hemisphere_color', ue.LinearColor(.24, .22, .19, 1))
if not levels.save_current_level():
    raise RuntimeError('Cannot save Town daylight')
for path, digest in before.items():
    if sha(Path(path)) != digest:
        raise RuntimeError('Protected asset changed '+path)
(backup.parent/'receipt.json').write_text(json.dumps(dict(validated_seeds=516, protected_assets=len(before),
    owned_packages=sorted(owned), backup=str(backup)), indent=2)+'\n')
print('ER_TOWN_SETUP_COMPLETE seeds=516 protected='+str(len(before)))
