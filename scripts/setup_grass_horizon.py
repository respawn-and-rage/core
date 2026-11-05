"""Create the distant grass finish in Unreal without editing shared materials."""
from pathlib import Path
import shutil
import unreal as ue

folder = "/Game/EclipseRecoil/Art/OutdoorBackdrop"
name = "M_OutdoorGrass"
path = f"{folder}/{name}"
assets = ue.EditorAssetLibrary
graph = ue.MaterialEditingLibrary
material = assets.load_asset(path) if assets.does_asset_exist(path) else None
if not material or assets.get_metadata_tag(material, "ERGrassHorizonRevision") != "2":
    texture = assets.load_asset("/Game/EclipseRecoil/Art/SharedMaterials/Source/Materials/Ground/Nature/Textures/T_GrassPlain01_BC")
    if not texture:
        raise RuntimeError("Shared grass texture is missing")
    tools = ue.AssetToolsHelpers.get_asset_tools()
    if material:
        source = Path(ue.Paths.convert_relative_path_to_full(ue.Paths.project_content_dir())) / f"{path.removeprefix('/Game/')}.uasset"
        backup = Path(ue.Paths.convert_relative_path_to_full(ue.Paths.project_saved_dir())) / "Backups/OutdoorBackdrop/M_OutdoorGrass.before-v2.uasset"
        if not backup.exists():
            backup.parent.mkdir(parents=True, exist_ok=True)
            shutil.copyfile(source, backup)
            if source.read_bytes() != backup.read_bytes():
                raise RuntimeError("Grass horizon backup did not verify")
        graph.delete_all_material_expressions(material)
    else:
        material = tools.create_asset(name, folder, ue.Material, ue.MaterialFactoryNew())

    def node(kind, x, y=0, **properties):
        expression = graph.create_material_expression(material, kind, x, y)
        for prop, value in properties.items():
            expression.set_editor_property(prop, value)
        return expression

    def connect(source, output, target, pin):
        if not graph.connect_material_expressions(source, output, target, pin):
            raise RuntimeError(f"Cannot connect {source.get_class().get_name()} to {target.get_class().get_name()}.{pin}")

    position = node(ue.MaterialExpressionWorldPosition, -1100)
    xy = node(ue.MaterialExpressionComponentMask, -900, r=True, g=True)
    connect(position, "", xy, "")
    scale = node(ue.MaterialExpressionDivide, -710, const_b=400.0)
    connect(xy, "", scale, "A")
    sample = node(ue.MaterialExpressionTextureSample, -520, texture=texture)
    connect(scale, "", sample, "UVs")
    variation = node(ue.MaterialExpressionLinearInterpolate, -320, const_a=.65, const_b=1.35)
    connect(sample, "R", variation, "Alpha")
    tint = node(ue.MaterialExpressionConstant3Vector, -320, 170, constant=ue.LinearColor(.14, .26, .055, 1))
    grass = node(ue.MaterialExpressionMultiply, -120)
    connect(variation, "", grass, "A")
    connect(tint, "", grass, "B")
    graph.connect_material_property(grass, "", ue.MaterialProperty.MP_BASE_COLOR)
    fill = node(ue.MaterialExpressionMultiply, 60, 150, const_b=.30)
    connect(grass, "", fill, "A")
    graph.connect_material_property(fill, "", ue.MaterialProperty.MP_EMISSIVE_COLOR)
    roughness = node(ue.MaterialExpressionConstant, 60, 330, r=1.0)
    graph.connect_material_property(roughness, "", ue.MaterialProperty.MP_ROUGHNESS)
    assets.set_metadata_tag(material, "ERGrassHorizonRevision", "2")
    graph.recompile_material(material)
    if not assets.save_loaded_asset(material):
        raise RuntimeError("Could not save the grass horizon material")
ue.log("ER_GRASS_HORIZON_SETUP_OK")
