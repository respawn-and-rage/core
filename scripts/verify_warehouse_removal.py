"""Unreal commandlet: verify retired warehouse packages and references stay absent."""
import unreal as ue
art='/Game/EclipseRecoil/Art/'
a=ue.EditorAssetLibrary;r=ue.AssetRegistryHelpers.get_asset_registry();r.search_all_assets(True)
options=ue.AssetRegistryDependencyOptions(True,True,True,True,True)
def retired(path):
    return path.startswith(art+'FireTraining/') and not path.startswith(art+'FireTraining/Procedural/')
for item in r.get_assets_by_path('/Game/EclipseRecoil',recursive=True):
    package=str(item.package_name)
    if retired(package):raise RuntimeError('Retired package remains '+package)
    for dependency in r.get_dependencies(package,options):
        if retired(str(dependency)):raise RuntimeError('Retired dependency '+package+' -> '+str(dependency))
for name in ['CentralBuilding','MiddleEasternAlleys','FireTraining','Rooms','Village']:
    recipe=a.load_asset(art+'MapTemplates/DA_'+name)
    if not recipe:raise RuntimeError('Missing recipe '+name)
    library=recipe.get_editor_property('theme_library')
    if not library:raise RuntimeError('Missing palette '+name)
    for theme in library.get_editor_property('themes'):
        for prop in ['wall','floor','ceiling','shell_steel','ladder_steel','deck_steel']:
            if not theme.get_editor_property(prop):raise RuntimeError('Missing finish '+name+' '+prop)
        if name in ['FireTraining','Rooms'] and 'concrete_seamless' not in theme.get_editor_property('floor').get_path_name():
            raise RuntimeError('Dry concrete is not bound to '+name)
print('ER_WAREHOUSE_FRESH_VERIFY_PASS templates=5')
