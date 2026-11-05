"""Create the owned, translucent, collision-free pool surface finish in Unreal."""
from pathlib import Path
import hashlib
import shutil
import unreal as ue

folder = "/Game/EclipseRecoil/Art/OutdoorBackdrop"
path = f"{folder}/M_PoolWater"
assets = ue.EditorAssetLibrary
graph = ue.MaterialEditingLibrary
material = assets.load_asset(path) if assets.does_asset_exist(path) else None
if not material or assets.get_metadata_tag(material, "ERPoolWaterRevision") != "1":
    if material:
        source = Path(ue.Paths.convert_relative_path_to_full(ue.Paths.project_content_dir())) / f"{path.removeprefix('/Game/')}.uasset"
        backup = Path(ue.Paths.convert_relative_path_to_full(ue.Paths.project_saved_dir())) / "Backups/OutdoorBackdrop/M_PoolWater.before-v1.uasset"
        if not backup.exists():
            backup.parent.mkdir(parents=True, exist_ok=True)
            shutil.copyfile(source, backup)
            if hashlib.sha256(source.read_bytes()).digest() != hashlib.sha256(backup.read_bytes()).digest():
                raise RuntimeError("Pool material backup did not verify")
        graph.delete_all_material_expressions(material)
    else:
        material = ue.AssetToolsHelpers.get_asset_tools().create_asset("M_PoolWater", folder, ue.Material, ue.MaterialFactoryNew())
    material.set_editor_property("blend_mode", ue.BlendMode.BLEND_TRANSLUCENT)
    material.set_editor_property("shading_model", ue.MaterialShadingModel.MSM_UNLIT)
    material.set_editor_property("two_sided", True)
    color = graph.create_material_expression(material, ue.MaterialExpressionConstant3Vector, -320, 0)
    color.set_editor_property("constant", ue.LinearColor(.055, .24, .32, 1))
    if not graph.connect_material_property(color, "", ue.MaterialProperty.MP_EMISSIVE_COLOR):
        raise RuntimeError("Could not connect water color")
    fresnel = graph.create_material_expression(material, ue.MaterialExpressionFresnel, -320, 220)
    fresnel.set_editor_property("exponent", 2.5)
    edge = graph.create_material_expression(material, ue.MaterialExpressionMultiply, -100, 220)
    edge.set_editor_property("const_b", .22)
    if not graph.connect_material_expressions(fresnel, "", edge, "A"):
        raise RuntimeError("Could not connect water Fresnel")
    opacity = graph.create_material_expression(material, ue.MaterialExpressionAdd, 120, 220)
    opacity.set_editor_property("const_b", .28)
    if not graph.connect_material_expressions(edge, "", opacity, "A") or not graph.connect_material_property(opacity, "", ue.MaterialProperty.MP_OPACITY):
        raise RuntimeError("Could not connect water opacity")
    graph.recompile_material(material)
    assets.set_metadata_tag(material, "ERPoolWaterRevision", "1")
    if not assets.save_loaded_asset(material):
        raise RuntimeError("Could not save pool material")
ue.log("ER_POOL_WATER_SETUP_OK")
