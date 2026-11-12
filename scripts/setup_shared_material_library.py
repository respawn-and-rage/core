"""Build the reviewed shared finish library and bind production templates.

Run through Unreal Python after compiling. Original assets are immutable. Copies
and shader adaptations live in the private maps repository. No maps, geometry,
damage tuning, foliage, lamps or sky packages are saved.
"""
from pathlib import Path
import hashlib
import json
import runpy
import zipfile
import unreal as ue

def main():
    root = Path(__file__).resolve().parents[1]
    a = ue.EditorAssetLibrary
    e = ue.MaterialEditingLibrary
    tools = ue.AssetToolsHelpers.get_asset_tools()
    registry = ue.AssetRegistryHelpers.get_asset_registry()
    registry.search_all_assets(True)
    registry.wait_for_completion()
    manifest = runpy.run_path(str(root/'scripts/shared_material_catalog.py'))['CATALOG']
    manifest_digest = hashlib.sha256((json.dumps(manifest, indent=2)+'\n').encode()).hexdigest()
    sources = sorted({p for family in manifest['families'].values() for p in family})
    if len(sources) != 72:
        raise RuntimeError('The reviewed catalog must contain exactly 72 sources')
    base = '/Game/EclipseRecoil/Art/SharedMaterials'
    source_root = '/Game/TwinmotionMaterials'
    owned_root = base + '/Source'
    map_base = '/Game/EclipseRecoil/Art/MapTemplates/'
    names = ['CentralBuilding', 'MiddleEasternAlleys', 'FireTraining', 'Rooms', 'Village', 'Town']
    out = root/'Saved/Testing/MaterialAudit'
    out.mkdir(parents=True, exist_ok=True)
    backup = root/'Saved/SharedMaterialLibraryBackup/before-shared-library.zip'
    marker = 'ERSharedMaterialsRevision'
    revision = '4'

    def disk(package):
        return root/('Content/'+package.removeprefix('/Game/')+'.uasset')

    def sha(path):
        return hashlib.sha256(path.read_bytes()).hexdigest()

    def load(path):
        obj = a.load_asset(path)
        if not obj:
            raise RuntimeError('Missing asset: '+path)
        return obj

    def save(obj):
        if not obj.get_path_name().startswith(('/Game/EclipseRecoil/Art/SharedMaterials/', map_base, '/Game/EclipseRecoil/Art/Destruction/DA_ER_BlockMaterials.')):
            raise RuntimeError('Refusing to save unowned package '+obj.get_path_name())
        if not a.save_loaded_asset(obj, False):
            raise RuntimeError('Cannot save '+obj.get_path_name())

    receipt_path = out/'shared-library-receipt.json'
    if receipt_path.exists():
        receipt = json.loads(receipt_path.read_text())
        if receipt.get('result') == 'PASS' and receipt.get('migration_revision') == revision and receipt.get('manifest_sha256') == manifest_digest:
            catalog = load(base+'/DA_SharedMapSurfaces')
            allowed = {material.get_path_name() for material in catalog.get_editor_property('surfaces') if material}
            if list(catalog.get_editor_property('source_packages')) != sources or len(allowed) != 72:
                raise RuntimeError('Completed library no longer matches the reviewed manifest')
            for name in names:
                recipe = load(map_base+'DA_'+name)
                library = recipe.get_editor_property('theme_library')
                if not recipe.get_editor_property('require_shared_surfaces') or not library.get_editor_property('require_shared_surfaces') or library.get_editor_property('shared_surfaces') != catalog:
                    raise RuntimeError('Completed template binding changed: '+name)
                for theme in library.get_editor_property('themes'):
                    surfaces = list(theme.get_editor_property('materials'))+list(theme.get_editor_property('grounds'))
                    surfaces += [theme.get_editor_property(k) for k in ['wall','floor','ceiling','shell_steel','ladder_steel','deck_steel','cargo_steel']]
                    if any(not material or material.get_path_name() not in allowed for material in surfaces) or theme.get_editor_property('preserve_cargo_paint'):
                        raise RuntimeError('Completed palette changed: '+name)
                if not recipe.validate_layout(42):
                    raise RuntimeError('Completed template validation failed: '+name)
            print('ER_SHARED_MATERIAL_LIBRARY_PASS already_complete=true')
            return

    changed_packages = [map_base+'DA_'+n for n in names] + [map_base+'DA_Theme_'+n for n in names] + ['/Game/EclipseRecoil/Art/Destruction/DA_ER_BlockMaterials']
    protected = {}
    for top in [root/'Assets/maps/Content', root/'Content/TwinmotionMaterials']:
        for file in top.rglob('*'):
            if file.suffix not in ['.uasset', '.umap'] or not file.is_file():
                continue
            relative = file.relative_to(top).with_suffix('').as_posix()
            package = '/Game/'+relative if top.name == 'Content' else '/Game/TwinmotionMaterials/'+relative
            if package not in changed_packages and not package.startswith(base+'/'):
                protected[str(file)] = sha(file)
    backup.parent.mkdir(parents=True, exist_ok=True)
    if not backup.exists():
        with zipfile.ZipFile(backup, 'w', zipfile.ZIP_DEFLATED) as z:
            hashes = {}
            for package in changed_packages:
                file = disk(package)
                relative = file.relative_to(root).as_posix()
                z.write(file, relative)
                hashes[relative] = sha(file)
            z.writestr('hashes.json', json.dumps(hashes, indent=2))
    with zipfile.ZipFile(backup) as z:
        for file, digest in json.loads(z.read('hashes.json')).items():
            if hashlib.sha256(z.read(file)).hexdigest() != digest:
                raise RuntimeError('Corrupt rollback: '+file)

    # Copy the dependency closure through Unreal, then remap only owned graphs.
    seen, pending = set(), list(sources)
    classes = {'Material', 'MaterialInstanceConstant', 'MaterialFunction', 'Texture2D', 'TextureCube', 'Texture2DArray'}
    options = ue.AssetRegistryDependencyOptions()
    while pending:
        package = pending.pop()
        if package in seen or not package.startswith(source_root+'/'):
            continue
        data = registry.get_assets_by_package_name(package)
        if not data or str(data[0].asset_class_path.asset_name) not in classes:
            continue
        seen.add(package)
        pending.extend(str(dep) for dep in registry.get_dependencies(package, options))
    destinations = {p: owned_root+p.removeprefix(source_root) for p in sorted(seen)}
    copies = {}
    for package, destination in destinations.items():
        if a.does_asset_exist(destination):
            obj = load(destination)
            if a.get_metadata_tag(obj, 'ERSharedMaterialSource') != package:
                raise RuntimeError('Unowned destination: '+destination)
        else:
            obj = a.duplicate_asset(package, destination)
            if not obj:
                raise RuntimeError('Cannot duplicate '+package)
            a.set_metadata_tag(obj, 'ERSharedMaterialSource', package)
        copies[package] = obj

    def remap(obj):
        return copies.get(obj.get_path_name().split('.')[0], obj) if obj else obj

    def expressions(obj):
        if isinstance(obj, ue.Material):
            return e.get_material_expressions(obj)
        if isinstance(obj, ue.MaterialFunction):
            return e.get_material_function_expressions(obj)
        return []

    # Composite normal references are texture settings, not graph nodes.
    for obj in copies.values():
        if isinstance(obj, ue.MaterialInterface):
            # Supplied streaming hints contain soft paths to obsolete source maps.
            # The world-space derivatives need their own streaming analysis anyway.
            if not ue.ERMapSurfaceLibrary.refresh_owned_material(obj):
                raise RuntimeError('Cannot refresh owned material '+obj.get_path_name())
            save(obj)
        if isinstance(obj, ue.Texture):
            composite = obj.get_editor_property('composite_texture')
            if composite and remap(composite) != composite:
                obj.set_editor_property('composite_texture', remap(composite))
                save(obj)
        elif isinstance(obj, ue.MaterialInstanceConstant):
            # Older supplied instances can retain overrides for removed parameters.
            values = list(obj.get_editor_property('texture_parameter_values'))
            for value in values:
                value.set_editor_property('parameter_value', remap(value.get_editor_property('parameter_value')))
            obj.set_editor_property('texture_parameter_values', values)
            save(obj)

    for package, obj in copies.items():
        if a.get_metadata_tag(obj, marker) == revision:
            continue
        if isinstance(obj, ue.MaterialInstanceConstant):
            source = load(package)
            e.set_material_instance_parent(obj, remap(source.get_editor_property('parent')))
            for param in e.get_texture_parameter_names(source):
                texture = e.get_material_instance_texture_parameter_value(source, param)
                if texture:
                    e.set_material_instance_texture_parameter_value(obj, param, remap(texture))
            e.update_material_instance(obj)
        else:
            nodes = expressions(obj)
            for node in nodes:
                if isinstance(node, ue.MaterialExpressionMaterialFunctionCall):
                    function = node.get_editor_property('material_function')
                    if function:
                        if remap(function) != function:
                            node.set_material_function(remap(function))
                try:
                    texture = node.get_editor_property('texture')
                except Exception:
                    texture = None
                if texture and remap(texture) != texture:
                    node.set_editor_property('texture', remap(texture))
            # The supplied triplanar mapping is object-local. Keep source physical
            # dimensions/tiling, but anchor it in world coordinates across cell meshes.
            if isinstance(obj, ue.MaterialFunction) and package.rsplit('/', 1)[1] in ['MF_Triplanar_UVs', 'MF_Triplanar_POM_UVs', 'MF_Triplanar_Mask']:
                for node in nodes:
                    if isinstance(node, ue.MaterialExpressionMaterialFunctionCall):
                        function = node.get_editor_property('material_function')
                        if function and function.get_name() in ['LocalPosition', 'ObjectScale']:
                            kind = function.get_name()
                            target = base+'/Functions/MF_Shared'+kind
                            if a.does_asset_exist(target):
                                adapter = load(target)
                            else:
                                adapter = a.duplicate_asset(function.get_path_name().split('.')[0], target)
                                cls = ue.MaterialExpressionWorldPosition if kind == 'LocalPosition' else ue.MaterialExpressionConstant3Vector
                                replacement = e.create_material_expression_in_function(adapter, cls, -1200, 0)
                                if kind == 'ObjectScale':
                                    replacement.set_editor_property('constant', ue.LinearColor(1, 1, 1, 1))
                                for output in e.get_material_function_expressions(adapter):
                                    if isinstance(output, ue.MaterialExpressionFunctionOutput):
                                        if not e.connect_material_expressions(replacement, '', output, ''):
                                            raise RuntimeError('Cannot adapt '+kind)
                                e.update_material_function(adapter)
                                a.set_metadata_tag(adapter, marker, revision)
                                save(adapter)
                            node.set_material_function(adapter)
                    elif isinstance(node, ue.MaterialExpressionTransform):
                        node.set_editor_property('transform_source_type', ue.MaterialVectorCoordTransformSource.TRANSFORMSOURCE_WORLD)
                        node.set_editor_property('transform_type', ue.MaterialVectorCoordTransform.TRANSFORM_WORLD)
            if isinstance(obj, ue.Material):
                obj.set_editor_property('used_with_instanced_static_meshes', True)
                e.recompile_material(obj)
            elif isinstance(obj, ue.MaterialFunction):
                e.update_material_function(obj)
        if isinstance(obj, (ue.Material, ue.MaterialFunction)) and not ue.ERMapSurfaceLibrary.restore_owned_function_inputs(load(package), obj):
            raise RuntimeError('Cannot restore function edges: '+package)
        a.set_metadata_tag(obj, marker, revision)
        save(obj)

    # Refresh transitive function caches after every graph has been remapped.
    functions = {p: obj for p, obj in copies.items() if isinstance(obj, ue.MaterialFunction)}
    finished = set()
    def refresh_function(package):
        if package in finished:
            return
        finished.add(package)
        for dep in registry.get_dependencies(package, options):
            if str(dep) in functions:
                refresh_function(str(dep))
        e.update_material_function(functions[package])
        save(functions[package])
    for package in functions:
        refresh_function(package)
    for obj in copies.values():
        if isinstance(obj, ue.Material):
            e.recompile_material(obj)
            save(obj)
        elif isinstance(obj, ue.MaterialInstanceConstant):
            e.update_material_instance(obj)
            save(obj)

    catalog_path = base+'/DA_SharedMapSurfaces'
    if a.does_asset_exist(catalog_path):
        catalog = load(catalog_path)
    else:
        factory = ue.DataAssetFactory()
        factory.set_editor_property('data_asset_class', ue.ERMapSurfaceLibrary)
        catalog = tools.create_asset('DA_SharedMapSurfaces', base, ue.ERMapSurfaceLibrary, factory)
    catalog.set_editor_property('surfaces', [copies[p] for p in sources])
    catalog.set_editor_property('source_packages', sources)
    catalog.set_editor_property('revision', int(revision))
    save(catalog)

    by_name = {p.rsplit('/', 1)[1].removeprefix('MI_'): copies[p] for p in sources}
    def finish(name):
        return by_name[name]

    enable_two_sided = runpy.run_path(str(root/'scripts/setup_village_two_sided_wood.py'))['enable_two_sided']
    enable_two_sided(finish('RoughPlanks'))
    save(finish('RoughPlanks'))
    tune_metal = runpy.run_path(str(root/'scripts/setup_neutral_metal_finishes.py'))['tune']
    tune_metal(finish('AluPanels150x150'), finish('GalvanisedSteel01'), save)
    tune_cargo = runpy.run_path(str(root/'scripts/setup_fire_training_cargo.py'))['tune_cargo']
    tune_cargo(finish('AluminiumSiding02'), save)
    # Order: Drywall, Brick, Concrete, Wood, Stone, Metal, Glass.
    specs = {
     'CentralBuilding': ('Urban', ['PlasterCoating02','BrickRough03','PouredConcrete01','OldPlanks','StoneWall01','CortenSteel01','Glass_Clear'], 'PouredConcrete01','AluPanels150x150','ConcreteSlabs01',['Asphalt02','GrassPlain01','Snow01']),
     'MiddleEasternAlleys': ('Stone', ['PlasterCoating03','BrickPainted01','PouredConcrete01','OldPlanks','StoneWall03','CortenSteel01','Glass_Clear'], 'StoneWall03','PavementSquare01','PavementSquare01',['DesertSand01']),
     'FireTraining': ('Warehouse', ['Plasterboards01','CinderblockBare02','PouredConcrete05','Plywood01','StoneWall01','CortenSteel01','Glass_Tinted'], 'PouredConcrete01','BareConcrete03','ConcreteSlabs01',['BareConcrete03']),
     'Rooms': ('Tunnels', ['Plasterboards01','CinderblockBare01','PouredConcrete01','Plywood02','StoneWall01','CortenSteel01','Glass_Frosted'], 'PouredConcrete01','BareConcrete03','PouredConcrete01',['BareConcrete03']),
     'Village': ('Jungle', ['PlasterCoating03','BrickRough03','BareConcrete03','RoughPlanks','StoneWall02','SlateRoofing01','Glass_Clear'], 'RoughPlanks','RoughPlanks','SlateRoofing01',['ForestBranches01','GroundDirt02','GrassPlain01']),
     'Town': ('Town', ['PlasterCoating02','BrickRough03','PouredConcrete01','Paneling01','StoneWall01','CortenSteel01','Glass_Clear'], 'PouredConcrete01','TilesConcrete05','ConcreteSlabs01',['AsphaltRoad02','PavementHerringbone02']),
    }
    for name, (label, physical, wall, floor, ceiling, grounds) in specs.items():
        library = load(map_base+'DA_Theme_'+name)
        theme = ue.ERMapThemePalette()
        for key, value in {'name':label, 'materials':[finish(n) for n in physical], 'wall':finish(wall), 'floor':finish(floor), 'ceiling':finish(ceiling), 'grounds':[finish(n) for n in grounds], 'shell_steel':finish('AluminiumSiding04'), 'ladder_steel':finish('GalvanisedSteel01'), 'deck_steel':finish('AluPanels150x150'), 'cargo_steel':finish('AluminiumSiding02' if name == 'FireTraining' else 'LightMetalFlakePaint'), 'preserve_cargo_paint':False}.items():
            theme.set_editor_property(key, value)
        library.set_editor_property('themes', [theme])
        library.set_editor_property('shared_surfaces', catalog)
        library.set_editor_property('require_shared_surfaces', True)
        if a.get_metadata_tag(library, marker) != revision:
            library.set_editor_property('revision', library.get_editor_property('revision')+1)
        a.set_metadata_tag(library, marker, revision)
        save(library)
        recipe = load(map_base+'DA_'+name)
        if a.get_metadata_tag(recipe, marker) != revision:
            recipe.set_editor_property('revision', recipe.get_editor_property('revision')+1)
        recipe.set_editor_property('theme_library', library)
        recipe.set_editor_property('require_shared_surfaces', True)
        a.set_metadata_tag(recipe, marker, revision)
        save(recipe)

    # Fallback surfaces used by debris and legacy cell callers must be selected too;
    # all numerical damage/calibration/effect properties remain unchanged.
    damage = load('/Game/EclipseRecoil/Art/Destruction/DA_ER_BlockMaterials')
    rows = list(damage.get_editor_property('materials'))
    kinds = [ue.ERBlockMaterial.DRYWALL,ue.ERBlockMaterial.BRICK,ue.ERBlockMaterial.CONCRETE,ue.ERBlockMaterial.WOOD,ue.ERBlockMaterial.STONE,ue.ERBlockMaterial.METAL,ue.ERBlockMaterial.GLASS]
    for row in rows:
        row.set_editor_property('surface', finish(specs['CentralBuilding'][1][kinds.index(row.get_editor_property('type'))]))
    damage.set_editor_property('materials', rows)
    save(damage)
    for file, digest in protected.items():
        if sha(Path(file)) != digest:
            raise RuntimeError('Protected package changed: '+file)
    # Fail if any shared derivative still depends on the unversioned download.
    registry.scan_paths_synchronous([base], force_rescan=True)
    leaks = {}
    for destination in destinations.values():
        deps = [str(p) for p in registry.get_dependencies(destination, options) if str(p).startswith(source_root+'/')]
        if deps:
            leaks[destination] = deps
    (out/'shared-library-leaks.json').write_text(json.dumps(leaks, indent=2)+'\n')
    if leaks:
        raise RuntimeError('Unremapped Twinmotion dependencies: '+str(len(leaks)))
    receipt = {'result':'PASS','migration_revision':revision,'catalog':catalog_path,'source_count':len(sources),'copied_packages':len(copies),'protected_packages':len(protected),'backup':str(backup),'manifest_sha256':manifest_digest,'exceptions':['vegetation','emissive lamps','sky'],'templates':list(specs)}
    (out/'shared-library-receipt.json').write_text(json.dumps(receipt, indent=2)+'\n')
    private_sources = root/'Assets/maps/SourceAssets/SharedMaterials'
    private_sources.mkdir(parents=True, exist_ok=True)
    (private_sources/'catalog.json').write_text(json.dumps(manifest, indent=2)+'\n')
    print('ER_SHARED_MATERIAL_LIBRARY_PASS '+json.dumps(receipt))


main()
