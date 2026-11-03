"""Create the Rooms recipe and level through Unreal; preserve completed assets.

Uses the three existing production themes without importing or editing art.
Run with UnrealEditor -run=pythonscript -script=/absolute/path/setup_rooms.py.
"""
import hashlib
import json
from pathlib import Path
import unreal as ue

root = Path(ue.Paths.project_dir()).resolve()
assets = ue.EditorAssetLibrary
tools = ue.AssetToolsHelpers.get_asset_tools()

def load(path):
    obj = assets.load_asset(path)
    if not obj:
        raise RuntimeError('Missing ' + path)
    return obj

def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()

protected = list((root/'Assets/maps/Content/EclipseRecoil/Art/MapTemplates').glob('*.uasset'))
protected += list((root/'Assets/maps/Content/EclipseRecoil/Maps').glob('*.umap'))
protected += [root/'Assets/maps/Content/EclipseRecoil/Art/Destruction/DA_ER_BlockMaterials.uasset']
before = {str(p): digest(p) for p in protected if p.exists()}
path = '/Game/EclipseRecoil/Art/MapTemplates/DA_Rooms'
if assets.does_asset_exist(path):
    template = load(path)
    if str(template.get_editor_property('template_id')) != 'rooms.indoor':
        raise RuntimeError('Existing DA_Rooms has a different owner')
else:
    central = load('/Game/EclipseRecoil/Art/MapTemplates/DA_CentralBuilding').get_editor_property('central_building')
    alleys = load('/Game/EclipseRecoil/Art/MapTemplates/DA_MiddleEasternAlleys').get_editor_property('town')
    fire = load('/Game/EclipseRecoil/Art/MapTemplates/DA_FireTraining').get_editor_property('fire_training')
    def first(recipe, key):
        values = list(recipe.get_editor_property(key))
        if not values or not values[0]:
            raise RuntimeError('Incomplete existing palette: ' + key)
        return values[0]
    palette = [
        ('Industrial', first(central, 'concrete_surfaces'), central.get_editor_property('deck_surface'),
         central.get_editor_property('steel_surface'), first(central, 'wood_surfaces')),
        ('Desert', alleys.get_editor_property('wall_surface'), alleys.get_editor_property('road_surface'),
         alleys.get_editor_property('roof_surface'), alleys.get_editor_property('wood_surface')),
        ('Warehouse', fire.get_editor_property('steel_surface'), fire.get_editor_property('ground_surface'),
         fire.get_editor_property('deck_surface'), first(fire, 'wood_surfaces')),
    ]
    themes = []
    for name, wall, floor, ceiling, wood in palette:
        theme = ue.ERRoomsTheme()
        for key, value in [('name', name), ('wall', wall), ('floor', floor), ('ceiling', ceiling), ('wood', wood)]:
            if value is None:
                raise RuntimeError('Incomplete Rooms theme ' + name + '/' + key)
            theme.set_editor_property(key, value)
        themes.append(theme)
    recipe = ue.ERRoomsRecipe()
    recipe.set_editor_property('themes', themes)
    recipe.set_editor_property('lamp_surface', load('/Game/EclipseRecoil/Art/Industrial/SharedSteel/M_ER_IndustrialLampEmissive'))
    candidate = ue.new_object(ue.ERMapTemplate)
    for key, value in [('template_id', 'rooms.indoor'), ('revision', 1),
                       ('source_reference', 'Original sealed room graph for 3v3 / 4v4; existing Industrial, Desert and Warehouse material themes'),
                       ('archetype', ue.ERMapArchetype.ROOMS), ('symmetry', ue.ERTemplateSymmetry.HALF_TURN),
                       ('rooms', recipe)]:
        candidate.set_editor_property(key, value)
    for seed in list(range(512)) + [7919, -17, 2147483647, -2147483648]:
        if not candidate.validate_layout(seed):
            raise RuntimeError('Invalid Rooms layout: ' + str(seed))
    factory = ue.DataAssetFactory()
    factory.set_editor_property('data_asset_class', ue.ERMapTemplate)
    template = tools.create_asset('DA_Rooms', '/Game/EclipseRecoil/Art/MapTemplates', ue.ERMapTemplate, factory)
    for key in ['template_id', 'revision', 'source_reference', 'archetype', 'symmetry', 'rooms']:
        template.set_editor_property(key, candidate.get_editor_property(key))
    assets.set_metadata_tag(template, 'ERRoomsRevision', '1')
    if not assets.save_loaded_asset(template, False):
        raise RuntimeError('Cannot save Rooms recipe')

map_path = '/Game/EclipseRecoil/Maps/L_Rooms'
levels = ue.get_editor_subsystem(ue.LevelEditorSubsystem)
actors = ue.get_editor_subsystem(ue.EditorActorSubsystem)
if not assets.does_asset_exist(map_path):
    if not levels.new_level(map_path):
        raise RuntimeError('Cannot create Rooms level')
    world = ue.get_editor_subsystem(ue.UnrealEditorSubsystem).get_editor_world()
    world.get_world_settings().set_editor_property('default_game_mode', assets.load_blueprint_class('/Game/EclipseRecoil/Core/BP_ERGameMode'))
    world.get_world_settings().set_editor_property('kill_z', -800)
    def spawn(cls, label):
        actor = actors.spawn_actor_from_class(cls, ue.Vector(0, 0, 100), ue.Rotator(pitch=0, yaw=0, roll=0))
        if not actor:
            raise RuntimeError('Cannot create ' + label)
        actor.set_actor_label(label)
        return actor
    generator = spawn(ue.ERProceduralMap, 'Rooms_Generator')
    generator.set_actor_location(ue.Vector(0, 0, 0), False, False)
    config = generator.get_editor_property('layout_config')
    config.set_editor_property('template_asset', template)
    generator.set_editor_property('layout_config', config)
    generator.set_editor_property('ceilings', True)
    generator.set_editor_property('material_catalog', load('/Game/EclipseRecoil/Art/Destruction/DA_ER_BlockMaterials'))
    generator.set_editor_property('explosion_effect_class', assets.load_blueprint_class('/Game/EclipseRecoil/Effects/Grenade/BP_ERGrenadeExplosion'))
    for team in ['ALPHA', 'OMEGA']:
        for index in range(8):
            spawn(ue.ERTeamPlayerStart, f'{team}_Start_{index}').set_editor_property('team', getattr(ue.ERTeam, team))
    if not levels.save_current_level():
        raise RuntimeError('Cannot save Rooms level')

for filename, sha in before.items():
    if digest(Path(filename)) != sha:
        raise RuntimeError('Existing asset changed: ' + filename)
receipt = root/'Assets/maps/SourceAssets/Rooms'
receipt.mkdir(parents=True, exist_ok=True)
(receipt/'recipe.json').write_text(json.dumps(dict(template=path, map=map_path, revision=1,
    rooms=[6, 8, 10], target_teams=[3, 4], symmetry='HalfTurn', themes=['Industrial', 'Desert', 'Warehouse'],
    source='Original project geometry; references to existing materials only', protected_sha256=before), indent=2)+'\n')
print('ER_ROOMS_SETUP_COMPLETE seeds=516 preserved=' + str(len(before)))
