"""Import the outdoor sunset and night panoramas and menu previews through Unreal Python."""
from hashlib import sha256
from pathlib import Path
import shutil

import unreal as ue

folder = "/Game/EclipseRecoil/Art/OutdoorBackdrop"
project = Path(ue.Paths.convert_relative_path_to_full(ue.Paths.project_dir()))
sources = project / "Assets/maps/SourceAssets/OutdoorBackdrop"
content = project / "Content" / folder.removeprefix("/Game/")
panoramas = [
    f"T_Sky_FreeStylized_{number}" for number in ("03", "12", "24", "111", "117", "132")
] + [f"T_Sky_Night_{number}" for number in ("17", "77", "85", "92")]
sizes = {name: (4096, 4096) for name in panoramas}
sizes.update({f"{name}_Preview": (512, 256) for name in panoramas})
protected = {path: sha256(path.read_bytes()).digest()
             for path in content.glob("*.uasset") if path.stem not in sizes}
assets = ue.EditorAssetLibrary
asset_tools = ue.AssetToolsHelpers.get_asset_tools()
source_digest_tag = "EROutdoorSunsetSourceSHA256"
backup = Path(ue.Paths.convert_relative_path_to_full(ue.Paths.project_saved_dir())) / "Backups/OutdoorSunsets"

digests = {name: sha256((sources / f"{name}.png").read_bytes()).hexdigest() for name in sizes}

for name, (width, height) in sizes.items():
    path = f"{folder}/{name}"
    package = content / f"{name}.uasset"
    texture = assets.load_asset(path) if assets.does_asset_exist(path) else None
    needs_import = not texture or assets.get_metadata_tag(texture, source_digest_tag) != digests[name]
    if needs_import:
        if package.is_file():
            old_digest = sha256(package.read_bytes()).hexdigest()
            backup.mkdir(parents=True, exist_ok=True)
            copy = backup / f"{name}-{old_digest}.uasset"
            if not copy.is_file():
                shutil.copy2(package, copy)
            if sha256(copy.read_bytes()).hexdigest() != old_digest:
                raise RuntimeError(f"Could not verify backup of {path}")
        task = ue.AssetImportTask()
        task.filename = str(sources / f"{name}.png")
        task.destination_path = folder
        task.destination_name = name
        task.automated = True
        task.save = False
        task.replace_existing = bool(texture)
        asset_tools.import_asset_tasks([task])
        imported = task.get_objects()
        texture = next((obj for obj in imported if obj.get_path_name() == f"{path}.{name}"), None)
    if not isinstance(texture, ue.Texture2D):
        raise RuntimeError(f"Could not import {path}")
    settings = {
        "max_texture_size": width,
        "never_stream": True,
        "compression_settings": ue.TextureCompressionSettings.TC_DEFAULT,
        "srgb": True,
    }
    changed = False
    for key, value in settings.items():
        if texture.get_editor_property(key) != value:
            texture.set_editor_property(key, value)
            changed = True
    if (texture.blueprint_get_size_x(), texture.blueprint_get_size_y()) != (width, height):
        raise RuntimeError(f"Expected {name} to be a {width}×{height} texture")
    if needs_import:
        assets.set_metadata_tag(texture, source_digest_tag, digests[name])
    if needs_import or changed or not package.is_file():
        if not assets.save_loaded_asset(texture, only_if_is_dirty=False):
            raise RuntimeError(f"Could not save {path}")

if any(sha256(path.read_bytes()).digest() != digest for path, digest in protected.items()):
    raise RuntimeError("An existing outdoor backdrop package changed")
ue.log(f"ER_OUTDOOR_SUNSET_SETUP_OK textures={len(sizes)}")
