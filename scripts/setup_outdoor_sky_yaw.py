"""Add a yaw parameter to the owned outdoor sky material through Unreal Python."""
from pathlib import Path
import hashlib
import shutil
import unreal as ue

path = "/Game/EclipseRecoil/Art/OutdoorBackdrop/M_OutdoorSky"
assets = ue.EditorAssetLibrary
graph = ue.MaterialEditingLibrary
sky = assets.load_asset(path)
if not isinstance(sky, ue.Material):
    raise RuntimeError(f"Missing owned material: {path}")
if assets.get_metadata_tag(sky, "EROutdoorSkyYawRevision") != "1":
    source = Path(ue.Paths.convert_relative_path_to_full(ue.Paths.project_content_dir())) / f"{path.removeprefix('/Game/')}.uasset"
    backup = Path(ue.Paths.convert_relative_path_to_full(ue.Paths.project_saved_dir())) / "Backups/OutdoorBackdrop/M_OutdoorSky.before-yaw.uasset"
    if not backup.exists():
        backup.parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(source, backup)
        if hashlib.sha256(source.read_bytes()).digest() != hashlib.sha256(backup.read_bytes()).digest():
            raise RuntimeError("Sky material backup did not verify")

    nodes = graph.get_material_expressions(sky)
    uv = next((node for node in nodes if isinstance(node, ue.MaterialExpressionCustom)
               and node.get_editor_property("output_type") == ue.CustomMaterialOutputType.CMOT_FLOAT2), None)
    camera = next((node for node in nodes if isinstance(node, ue.MaterialExpressionCameraVectorWS)), None)
    if not uv or not camera:
        raise RuntimeError("The owned sky projection graph has changed")
    yaw = next((node for node in nodes if isinstance(node, ue.MaterialExpressionScalarParameter)
                and str(node.get_editor_property("parameter_name")) == "PanoramaYawRadians"), None)
    if not yaw:
        yaw = graph.create_material_expression(sky, ue.MaterialExpressionScalarParameter, -650, 180)
        yaw.set_editor_property("parameter_name", "PanoramaYawRadians")
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
    if not graph.connect_material_expressions(camera, "", uv, "CameraDirection"):
        raise RuntimeError("Could not reconnect camera direction")
    if not graph.connect_material_expressions(yaw, "", uv, "YawOffset"):
        raise RuntimeError("Could not connect panorama yaw")
    graph.recompile_material(sky)
    assets.set_metadata_tag(sky, "EROutdoorSkyYawRevision", "1")
    if not assets.save_loaded_asset(sky):
        raise RuntimeError("Could not save the sky material")
ue.log("ER_OUTDOOR_SKY_YAW_OK")
