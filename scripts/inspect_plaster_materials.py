"""Read-only plaster family audit; export source textures into Saved/Testing.

Run through Unreal Python. Does not save or modify any content package.
Exports are source maps, not rendered material previews.
"""
import json
from pathlib import Path
import unreal as ue

root = Path(ue.Paths.project_dir()).resolve()
out = root / 'Saved/Testing/PlasterMaterials'
out.mkdir(parents=True, exist_ok=True)
assets = ue.EditorAssetLibrary
edit = ue.MaterialEditingLibrary
registry = ue.AssetRegistryHelpers.get_asset_registry()
registry.search_all_assets(True)
registry.wait_for_completion()
paths = [
    '/Game/EclipseRecoil/Art/ProceduralTextures/M_ER_PBR_Plaster001',
    '/Game/EclipseRecoil/Art/MiddleEasternAlleys/M_ER_AlleyPlaster',
]
folder = '/Game/TwinmotionMaterials/Materials/Wall_coverings'
paths += [folder + '/' + name for name in [
    'MI_Plasterboards01', 'MI_PaintedWallMatte', 'MI_PaintedWallSatin',
    *['MI_PlasterCoating%02d' % i for i in range(1, 6)],
    *['MI_RoughcastCoating%02d' % i for i in range(1, 6)],
]]
rows = []
exported = {}
for asset_path in paths:
    obj = assets.load_asset(asset_path)
    if not obj:
        raise RuntimeError('Missing material: ' + asset_path)
    row = {'path': obj.get_path_name(), 'name': obj.get_name(),
           'textures': {}, 'scalars': {}, 'vectors': {}, 'switches': {}}
    if isinstance(obj, ue.MaterialInstanceConstant):
        row['parent'] = obj.get_editor_property('parent').get_path_name()
        for kind, key in [('texture', 'textures'), ('scalar', 'scalars'), ('vector', 'vectors'), ('static_switch', 'switches')]:
            for param in getattr(edit, 'get_' + kind + '_parameter_names')(obj):
                value = getattr(edit, 'get_material_instance_' + kind + '_parameter_value')(obj, param)
                if kind == 'texture':
                    row[key][str(param)] = value.get_path_name() if value else None
                elif kind == 'vector':
                    row[key][str(param)] = [value.r, value.g, value.b, value.a]
                else:
                    row[key][str(param)] = value
    material = obj.get_base_material()
    row['base_material'] = material.get_path_name()
    row['blend_mode'] = str(material.get_editor_property('blend_mode'))
    row['instanced_meshes'] = material.get_editor_property('used_with_instanced_static_meshes')
    row['tangent_space_normal'] = material.get_editor_property('tangent_space_normal')
    row['projection_metadata'] = assets.get_metadata_tag(obj, 'ERProjection')
    if isinstance(obj, ue.MaterialInstanceConstant):
        texture_paths = sorted({p for p in row['textures'].values() if p})
        textures = [assets.load_asset(p) for p in texture_paths]
    else:
        textures = edit.get_used_textures(material)
        if not textures:
            # NullRHI has no compiled material resource for GetUsedTextures.
            # Read serialized dependencies instead, without changing the graph.
            deps = registry.get_dependencies(asset_path, ue.AssetRegistryDependencyOptions())
            textures = []
            for dep in deps:
                for data in registry.get_assets_by_package_name(dep):
                    if str(data.asset_class_path.asset_name) == 'Texture2D':
                        textures.append(data.get_asset())
    row['used_textures'] = []
    for texture in textures:
        texture_path = texture.get_path_name()
        entry = {'path': texture_path, 'name': texture.get_name(),
                 'srgb': texture.get_editor_property('srgb'),
                 'compression': str(texture.get_editor_property('compression_settings'))}
        # Export only plaster-owned maps, never engine fallbacks.
        if texture_path.startswith(('/Game/TwinmotionMaterials/Materials/Wall_coverings/',
                                    '/Game/EclipseRecoil/Art/ProceduralTextures/Plaster001/',
                                    '/Game/EclipseRecoil/Art/Industrial/SketchfabTextures/')):
            if texture_path not in exported:
                dest = out / (texture.get_name() + '.png')
                task = ue.AssetExportTask()
                task.object = texture
                task.filename = str(dest)
                task.automated = True
                task.prompt = False
                task.replace_identical = True
                task.exporter = ue.TextureExporterPNG()
                if not dest.is_file() and not ue.Exporter.run_asset_export_task(task):
                    raise RuntimeError('Cannot export: ' + texture_path)
                exported[texture_path] = str(dest)
            entry['export'] = exported[texture_path]
        row['used_textures'].append(entry)
    rows.append(row)
    (out / 'materials.json').write_text(json.dumps(rows, indent=2) + '\n')
print('ER_PLASTER_AUDIT_PASS materials=' + str(len(rows)) + ' textures=' + str(len(exported)))
