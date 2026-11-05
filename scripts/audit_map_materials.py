"""Read-only audit of template material dependencies and imported mesh slots."""
from pathlib import Path
import json
import unreal as ue

root = Path(__file__).resolve().parents[1]
out = root / 'Saved/Testing/MaterialAudit'
out.mkdir(parents=True, exist_ok=True)
registry = ue.AssetRegistryHelpers.get_asset_registry()
registry.search_all_assets(True)
registry.wait_for_completion()
options = ue.AssetRegistryDependencyOptions()
report = {}
for name in ['CentralBuilding', 'MiddleEasternAlleys', 'FireTraining', 'Rooms', 'Village', 'Town']:
    template_path = '/Game/EclipseRecoil/Art/MapTemplates/DA_' + name
    seen, pending, materials, meshes = set(), [template_path], {}, {}
    while pending:
        package = pending.pop()
        if package in seen or not package.startswith('/Game/'):
            continue
        seen.add(package)
        for data in registry.get_assets_by_package_name(package):
            cls = str(data.asset_class_path.asset_name)
            if cls in ['Material', 'MaterialInstanceConstant']:
                obj = data.get_asset()
                materials[package] = {'blend_mode': str(obj.get_base_material().get_editor_property('blend_mode'))}
            elif cls == 'StaticMesh':
                mesh = data.get_asset()
                meshes[package] = [str(slot.material_interface.get_path_name()) if slot.material_interface else None for slot in mesh.get_editor_property('static_materials')]
        pending.extend(str(dep) for dep in registry.get_dependencies(package, options))
    report[name] = {'material_dependencies': materials, 'mesh_slots': meshes}
(out / 'template-material-dependencies.json').write_text(json.dumps(report, indent=2) + '\n')
print('ER_MAP_MATERIAL_AUDIT_PASS ' + str({k: {'materials': len(v['material_dependencies']), 'meshes': len(v['mesh_slots'])} for k, v in report.items()}))
