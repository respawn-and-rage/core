"""Create the Rocky recipe, shared palette and playable level in Unreal Python."""

import hashlib
import json
from pathlib import Path
from zipfile import ZIP_DEFLATED, ZipFile

import unreal as ue


ART = '/Game/EclipseRecoil/Art/'
TEMPLATES = ART + 'MapTemplates/'
LEVEL = '/Game/EclipseRecoil/Maps/L_Rocky'
PACKAGES = (TEMPLATES + 'DA_Theme_Rocky', TEMPLATES + 'DA_Rocky', LEVEL)
OWNED = 'ERRockySetup'
SEEDS = (*range(512), 7919, -17, 2147483647, -2147483648)
ASSETS = ue.EditorAssetLibrary


def load(path):
    asset = ASSETS.load_asset(path)
    if not asset:
        raise RuntimeError('Missing asset ' + path)
    return asset


def set_properties(obj, **properties):
    for key, value in properties.items():
        obj.set_editor_property(key, value)


def digest(path):
    with path.open('rb') as handle:
        hasher = hashlib.sha256()
        for chunk in iter(lambda: handle.read(1024 * 1024), b''):
            hasher.update(chunk)
        return hasher.hexdigest()


def save_data(package, cls, **properties):
    if ASSETS.does_asset_exist(package):
        asset = load(package)
    else:
        folder, name = package.rsplit('/', 1)
        factory = ue.DataAssetFactory()
        factory.set_editor_property('data_asset_class', cls)
        asset = ue.AssetToolsHelpers.get_asset_tools().create_asset(name, folder, cls, factory)
        if not asset:
            raise RuntimeError('Cannot create ' + package)
        ASSETS.set_metadata_tag(asset, OWNED, '1')
    set_properties(asset, **properties)
    if not ASSETS.save_loaded_asset(asset, False):
        raise RuntimeError('Cannot save ' + package)
    return asset


def create_level(recipe):
    levels = ue.get_editor_subsystem(ue.LevelEditorSubsystem)
    actors = ue.get_editor_subsystem(ue.EditorActorSubsystem)
    if not levels.new_level(LEVEL):
        raise RuntimeError('Cannot create ' + LEVEL)
    world = ue.get_editor_subsystem(ue.UnrealEditorSubsystem).get_editor_world()
    game_mode = ASSETS.load_blueprint_class('/Game/EclipseRecoil/Core/BP_ERGameMode')
    explosion = ASSETS.load_blueprint_class('/Game/EclipseRecoil/Effects/Grenade/BP_ERGrenadeExplosion')
    if not game_mode or not explosion:
        raise RuntimeError('Missing gameplay Blueprint dependencies')
    set_properties(world.get_world_settings(), default_game_mode=game_mode, kill_z=-800)

    def spawn(cls, label, position=(0, 0, 0)):
        actor = actors.spawn_actor_from_class(cls, ue.Vector(*position), ue.Rotator(pitch=0, yaw=0, roll=0))
        if not actor:
            raise RuntimeError('Cannot place ' + label)
        actor.set_actor_label(label)
        return actor

    generator = spawn(ue.ERProceduralMap, 'Rocky_Generator')
    layout = generator.get_editor_property('layout_config')
    layout.set_editor_property('template_asset', recipe)
    set_properties(generator, layout_config=layout, ceilings=False, explosion_effect_class=explosion,
                   material_catalog=load(ART + 'Destruction/DA_ER_BlockMaterials'))
    for label, team in (('Alpha', ue.ERTeam.ALPHA), ('Omega', ue.ERTeam.OMEGA)):
        for index in range(10):
            spawn(ue.ERTeamPlayerStart, f'{label}_Start_{index}', (0, 0, 100)).set_editor_property('team', team)
    for index in range(12):
        spawn(ue.ERBotPatrolPoint, f'Rocky_Patrol_{index}', (0, 0, 100)).set_editor_property('route_index', index)

    sun = spawn(ue.DirectionalLight, 'Rocky_Sun', (0, 0, 2500))
    sun.set_actor_rotation(ue.Rotator(pitch=-49, yaw=-35, roll=0), False)
    sun.light_component.set_mobility(ue.ComponentMobility.MOVABLE)
    set_properties(sun.light_component, intensity=4.0, atmosphere_sun_light=True,
                   light_color=ue.Color(r=255, g=246, b=232, a=255))
    sky = spawn(ue.SkyLight, 'Rocky_Sky', (0, 0, 2200))
    sky.light_component.set_mobility(ue.ComponentMobility.MOVABLE)
    set_properties(sky.light_component, intensity=1.7, real_time_capture=True, lower_hemisphere_is_black=True,
                   lower_hemisphere_color=ue.LinearColor(.24, .22, .19, 1))
    spawn(ue.SkyAtmosphere, 'Rocky_Atmosphere')
    ASSETS.set_metadata_tag(world, OWNED, '1')
    if not levels.save_current_level():
        raise RuntimeError('Cannot save ' + LEVEL)


def main():
    root = Path(ue.Paths.project_dir()).resolve()
    content = root / 'Assets/maps/Content'
    owned = {content / (path.removeprefix('/Game/') + suffix)
             for path, suffix in zip(PACKAGES, ('.uasset', '.uasset', '.umap'))}
    protected = {path: digest(path) for path in content.rglob('*')
                 if path.suffix in ('.uasset', '.umap') and path not in owned}
    for package in PACKAGES:
        if ASSETS.does_asset_exist(package) and ASSETS.get_metadata_tag(load(package), OWNED) != '1':
            raise RuntimeError('Refusing to overwrite unowned package ' + package)
    backup = root / 'Saved/RockyBackup/before-natural-rocky.zip'
    backup.parent.mkdir(parents=True, exist_ok=True)
    if not backup.exists():
        with ZipFile(backup, 'w', ZIP_DEFLATED) as archive:
            hashes = {}
            for path in sorted(owned):
                if path.is_file():
                    name = path.relative_to(content).as_posix()
                    hashes[name] = digest(path)
                    archive.write(path, name)
            archive.writestr('hashes.json', json.dumps(hashes))
    with ZipFile(backup) as archive:
        for name, value in json.loads(archive.read('hashes.json')).items():
            if hashlib.sha256(archive.read(name)).hexdigest() != value:
                raise RuntimeError('Invalid Rocky rollback ' + name)

    catalog = load(ART + 'SharedMaterials/DA_SharedMapSurfaces')
    finishes = {surface.get_name().removeprefix('MI_'): surface for surface in catalog.get_editor_property('surfaces')}
    palette = ue.ERMapThemePalette()
    physical = ('PlasterCoating02', 'BrickRough03', 'PouredConcrete01', 'RoughPlanks',
                'RocksMossy01', 'AluPanels150x150', 'Glass_Clear')
    set_properties(palette, name='Rocky', materials=[finishes[name] for name in physical],
                   grounds=[finishes[name] for name in ('GroundGravel01', 'GroundCracked01')],
                   wall=finishes['RoughPlanks'], floor=finishes['RoughPlanks'], ceiling=finishes['OldPlanks'],
                   shell_steel=finishes['AluminiumSiding04'], ladder_steel=finishes['GalvanisedSteel01'],
                   deck_steel=finishes['AluPanels150x150'], cargo_steel=finishes['LightMetalFlakePaint'])
    library_properties = dict(themes=[palette], shared_surfaces=catalog, require_shared_surfaces=True, revision=3)
    library = ue.new_object(ue.ERMapThemeLibrary)
    set_properties(library, **library_properties)
    recipe_properties = dict(template_id='rocky.canyon', revision=3, archetype=ue.ERMapArchetype.ROCKY,
                             symmetry=ue.ERTemplateSymmetry.HALF_TURN, arena_size=ue.Vector(6000, 6000, 1500),
                             require_shared_surfaces=True, theme_library=library,
                             source_reference='Original terrain guided by the height changes and rocky cover of Lunacy')
    complete = (all(ASSETS.does_asset_exist(path) for path in PACKAGES)
                and all(load(path).get_editor_property('revision') == 3 for path in PACKAGES[:2]))
    candidate = load(PACKAGES[1]) if complete else ue.new_object(ue.ERMapTemplate)
    if not complete:
        set_properties(candidate, **recipe_properties)
    for seed in SEEDS:
        if not candidate.validate_layout(seed, 2):
            raise RuntimeError(f'Rocky rejected seed {seed}')

    if not complete:
        recipe_properties['theme_library'] = save_data(PACKAGES[0], ue.ERMapThemeLibrary, **library_properties)
        recipe = save_data(PACKAGES[1], ue.ERMapTemplate, **recipe_properties)
        if not ASSETS.does_asset_exist(LEVEL):
            create_level(recipe)
    if not all(path.is_file() for path in owned):
        raise RuntimeError('Rocky packages must be saved in the private maps repository')
    for path, value in protected.items():
        if digest(path) != value:
            raise RuntimeError('Protected asset changed ' + str(path))
    print(f'ER_ROCKY_SETUP_COMPLETE seeds={len(SEEDS)} protected={len(protected)}')


main()
