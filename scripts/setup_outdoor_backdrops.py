"""Run with Unreal Python and the supplied Atmospheric_Skybox Content mounted read-only."""
from pathlib import Path
import hashlib
import shutil
import unreal as ue

SOURCE = "/Game/Atmospheric_Skybox/Textures"
TARGET = "/Game/EclipseRecoil/Art/OutdoorBackdrop"
assets = ue.EditorAssetLibrary
graph = ue.MaterialEditingLibrary
factory = ue.AssetToolsHelpers.get_asset_tools()
content = Path(ue.Paths.convert_relative_path_to_full(ue.Paths.project_content_dir()))
dest = content / TARGET.removeprefix("/Game/")
backup = Path(ue.Paths.convert_relative_path_to_full(ue.Paths.project_saved_dir())) / "Backups/OutdoorBackdrop"
def hashes(root):
    return {p.relative_to(root): hashlib.sha256(p.read_bytes()).hexdigest()
            for p in root.rglob("*.uasset")}

source_files = content / SOURCE.removeprefix("/Game/")
original = hashes(source_files)
if len(original) != 15:
    raise RuntimeError("Mount the supplied Atmospheric_Skybox Content before running the migration")
if dest.exists() and not backup.exists():
    shutil.copytree(dest, backup)
    if hashes(dest) != hashes(backup):
        raise RuntimeError("Outdoor backdrop backup did not verify")

def material(name):
    path = f"{TARGET}/{name}"
    existing = assets.load_asset(path) if assets.does_asset_exist(path) else None
    if existing:
        return existing, False
    return factory.create_asset(name, TARGET, ue.Material, ue.MaterialFactoryNew()), True

sky, created = material("M_OutdoorSky")
if created:
    sky.set_editor_property("two_sided", True)
    sky.set_editor_property("is_sky", True)
    sky.set_editor_property("shading_model", ue.MaterialShadingModel.MSM_UNLIT)
    camera = graph.create_material_expression(sky, ue.MaterialExpressionCameraVectorWS, -650, 0)
    uv = graph.create_material_expression(sky, ue.MaterialExpressionCustom, -420, 0)
    uv.set_editor_property("output_type", ue.CustomMaterialOutputType.CMOT_FLOAT2)
    camera_input = ue.CustomInput()
    camera_input.set_editor_property("input_name", "CameraDirection")
    yaw_input = ue.CustomInput()
    yaw_input.set_editor_property("input_name", "YawOffset")
    uv.set_editor_property("inputs", [camera_input, yaw_input])
    uv.set_editor_property("code", """
float3 ray = -normalize(CameraDirection);
float radius = acos(saturate(ray.z)) * (0.48 / 1.5707963);
float s = sin(YawOffset);
float c = cos(YawOffset);
float2 direction = float2(c * ray.x - s * ray.y, s * ray.x + c * ray.y);
return 0.5 + direction * rsqrt(max(dot(direction, direction), 1e-8)) * radius;
""")
    graph.connect_material_expressions(camera, "", uv, "CameraDirection")
    yaw = graph.create_material_expression(sky, ue.MaterialExpressionScalarParameter, -650, 180)
    yaw.set_editor_property("parameter_name", "PanoramaYawRadians")
    graph.connect_material_expressions(yaw, "", uv, "YawOffset")
    sampler = graph.create_material_expression(sky, ue.MaterialExpressionTextureSampleParameter2D, -180, 0)
    sampler.set_editor_property("parameter_name", "Panorama")
    sampler.set_editor_property("texture", ue.load_asset("/Engine/EngineResources/DefaultTexture"))
    graph.connect_material_expressions(uv, "", sampler, "UVs")
    graph.connect_material_property(sampler, "RGB", ue.MaterialProperty.MP_EMISSIVE_COLOR)
    graph.recompile_material(sky)
    if not assets.save_loaded_asset(sky):
        raise RuntimeError("Could not save the sky material")

ground, created = material("M_OutdoorHorizon")
if created:
    tint = graph.create_material_expression(ground, ue.MaterialExpressionVectorParameter, -220, 0)
    tint.set_editor_property("parameter_name", "Tint")
    tint.set_editor_property("default_value", ue.LinearColor(.23, .21, .17, 1))
    graph.connect_material_property(tint, "", ue.MaterialProperty.MP_BASE_COLOR)
    rough = graph.create_material_expression(ground, ue.MaterialExpressionConstant, -220, 150)
    rough.set_editor_property("r", 1.0)
    graph.connect_material_property(rough, "", ue.MaterialProperty.MP_ROUGHNESS)
    graph.recompile_material(ground)
    if not assets.save_loaded_asset(ground):
        raise RuntimeError("Could not save the horizon material")

for index in range(1, 16):
    name = f"T_Sky_CSS_{index:03d}"
    path = f"{TARGET}/{name}"
    texture = assets.load_asset(path) if assets.does_asset_exist(path) else assets.duplicate_asset(f"{SOURCE}/{name}", path)
    if not isinstance(texture, ue.Texture2D):
        raise RuntimeError(f"Could not duplicate {name}")
    texture.set_editor_property("max_texture_size", 2048)
    texture.set_editor_property("never_stream", True)
    texture.set_editor_property("compression_settings", ue.TextureCompressionSettings.TC_DEFAULT)
    texture.set_editor_property("srgb", True)
    if not assets.save_loaded_asset(texture):
        raise RuntimeError(f"Could not save {name}")

if hashes(source_files) != original:
    raise RuntimeError("The source skybox packages changed")
ue.log("ER_OUTDOOR_BACKDROP_SETUP_OK skies=15")
