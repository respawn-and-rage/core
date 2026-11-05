"""Keep the panorama visible in the main and real-time Skylight captures."""
from pathlib import Path
import hashlib
import shutil
import unreal as ue

asset_path = "/Game/EclipseRecoil/Art/OutdoorBackdrop/M_OutdoorSky"
assets = ue.EditorAssetLibrary
sky = assets.load_asset(asset_path)
if not isinstance(sky, ue.Material):
    raise RuntimeError(f"Missing owned sky material: {asset_path}")

if not sky.get_editor_property("is_sky"):
    package = Path(ue.Paths.convert_relative_path_to_full(ue.Paths.project_content_dir())) / f"{asset_path.removeprefix('/Game/')}.uasset"
    backup = Path(ue.Paths.convert_relative_path_to_full(ue.Paths.project_saved_dir())) / "Backups/OutdoorBackdrop/M_OutdoorSky.before-visual-only.uasset"
    if not backup.exists():
        backup.parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(package, backup)
        if hashlib.sha256(package.read_bytes()).digest() != hashlib.sha256(backup.read_bytes()).digest():
            raise RuntimeError("Sky material backup did not verify")
    sky.set_editor_property("is_sky", True)
    ue.MaterialEditingLibrary.recompile_material(sky)
    if not assets.save_loaded_asset(sky):
        raise RuntimeError("Could not save the sky material")

if not sky.get_editor_property("is_sky"):
    raise RuntimeError("Sky material is not flagged as a sky")
ue.log("ER_OUTDOOR_SKY_CAPTURE_OK")
