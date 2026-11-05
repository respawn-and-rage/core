"""Read-only Twinmotion family inventory and base-color exports.

Run through Unreal Python with -ERMaterialFamily=Brick (the default).
Optional -ERMaterialExtraAssets accepts comma-separated Twinmotion asset paths.
Add -ERMaterialExportDetailMaps to export normal and roughness maps too.
Use -ERMaterialMetadataOnly when source textures do not show the finish (glass).
Only writes reports and PNG exports under Saved/Testing; never saves packages.
"""
import json
import re
from pathlib import Path
import unreal as ue

root = Path(__file__).resolve().parents[1]
match = re.search(r'(?:^|\s)-ERMaterialFamily=([A-Za-z_]+)(?:\s|$)', ue.SystemLibrary.get_command_line())
family = match.group(1) if match else 'Brick'
source = root / 'Content/TwinmotionMaterials/Materials' / family
if not source.is_dir():
    raise RuntimeError('Unknown Twinmotion family: ' + family)
out = root / 'Saved/Testing' / (family + 'Materials')
out.mkdir(parents=True, exist_ok=True)
a = ue.EditorAssetLibrary
e = ue.MaterialEditingLibrary
rows = []
asset_paths = ['/Game/TwinmotionMaterials/Materials/' + family + '/' + disk.stem
               for disk in sorted(source.glob('MI_*.uasset'))]
extra = re.search(r'(?:^|\s)-ERMaterialExtraAssets=([A-Za-z0-9_/,]+)(?:\s|$)', ue.SystemLibrary.get_command_line())
if extra:
    for asset_path in extra.group(1).split(','):
        if not asset_path.startswith('/Game/TwinmotionMaterials/Materials/'):
            raise RuntimeError('Extra asset must belong to Twinmotion: ' + asset_path)
        if asset_path not in asset_paths:
            asset_paths.append(asset_path)
for asset_path in asset_paths:
    obj = a.load_asset(asset_path)
    if not isinstance(obj, ue.MaterialInstanceConstant):
        raise RuntimeError('Missing material instance: ' + asset_path)
    row = {'name': obj.get_name(), 'path': obj.get_path_name(),
           'parent': obj.get_editor_property('parent').get_path_name(),
           'textures': {}, 'scalars': {}, 'vectors': {}, 'switches': {}}
    for kind, key in [('texture', 'textures'), ('scalar', 'scalars'), ('vector', 'vectors'), ('static_switch', 'switches')]:
        for param in getattr(e, 'get_' + kind + '_parameter_names')(obj):
            value = getattr(e, 'get_material_instance_' + kind + '_parameter_value')(obj, param)
            if kind == 'texture':
                row[key][str(param)] = value.get_path_name() if value else None
            elif kind == 'vector':
                row[key][str(param)] = [value.r, value.g, value.b, value.a]
            else:
                row[key][str(param)] = value
    base_material = obj.get_base_material()
    row['base_material'] = base_material.get_path_name()
    row['blend_mode'] = str(base_material.get_editor_property('blend_mode'))
    row['two_sided'] = base_material.get_editor_property('two_sided')
    if '-ERMaterialMetadataOnly' in ue.SystemLibrary.get_command_line():
        rows.append(row)
        (out / 'materials.json').write_text(json.dumps(rows, indent=2) + '\n')
        continue
    color_path = row['textures'].get('Color Map')
    if not color_path:
        raise RuntimeError('No Color Map parameter: ' + asset_path)
    texture = a.load_asset(color_path)
    dest = out / (texture.get_name() + '.png')
    task = ue.AssetExportTask()
    task.object = texture
    task.filename = str(dest)
    task.automated = True
    task.prompt = False
    task.replace_identical = True
    task.exporter = ue.TextureExporterPNG()
    if not ue.Exporter.run_asset_export_task(task):
        raise RuntimeError('Cannot export: ' + color_path)
    row['color_export'] = str(dest)
    if '-ERMaterialExportDetailMaps' in ue.SystemLibrary.get_command_line():
        row['detail_exports'] = {}
        for parameter in ['Normal Map', 'Roughness Map']:
            texture_path = row['textures'].get(parameter)
            if not texture_path or not texture_path.startswith('/Game/TwinmotionMaterials/'):
                continue
            detail = a.load_asset(texture_path)
            target = out / (detail.get_name() + '.png')
            task.object = detail
            task.filename = str(target)
            if not ue.Exporter.run_asset_export_task(task):
                raise RuntimeError('Cannot export: ' + texture_path)
            row['detail_exports'][parameter] = str(target)
    rows.append(row)
    (out / 'materials.json').write_text(json.dumps(rows, indent=2) + '\n')
print('ER_TWINMOTION_FAMILY_PASS family=' + family + ' materials=' + str(len(rows)))
