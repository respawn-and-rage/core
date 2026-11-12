"""Brighten the owned muzzle flash material used by the character Blueprint."""

import hashlib
from pathlib import Path
import shutil

import unreal as ue


assets = ue.EditorAssetLibrary
materials = ue.MaterialEditingLibrary
path = "/Game/EclipseRecoil/Effects/Shots/M_ER_MuzzleFlashSoft"
character_path = "/Game/EclipseRecoil/Core/BP_ERCharacter"
character = ue.get_default_object(assets.load_blueprint_class(character_path))
flash = character.get_editor_property("muzzle_flash_material")
assert flash.get_path_name().split(".")[0] == path, flash.get_path_name()

project = Path(ue.Paths.project_dir()).resolve()
package = project / "Content" / (path.removeprefix("/Game/") + ".uasset")
protected = [file for file in package.parent.glob("*.uasset") if file != package]
protected.append(project / "Content" / (character_path.removeprefix("/Game/") + ".uasset"))


def digest(file):
    return hashlib.sha256(file.read_bytes()).hexdigest()


if assets.get_metadata_tag(flash, "ERWarmMuzzleFlash") == "1":
    ue.log("ER_MUZZLE_MATERIAL_ALREADY_APPLIED")
else:
    backup = project / "Saved" / "Testing" / "MuzzleFlashBackup" / package.name
    backup.parent.mkdir(parents=True, exist_ok=True)
    if not backup.exists():
        shutil.copy2(package, backup)
    hashes = {file: digest(file) for file in protected}

    color = materials.get_material_property_input_node(flash, ue.MaterialProperty.MP_EMISSIVE_COLOR)
    opacity = materials.get_material_property_input_node(flash, ue.MaterialProperty.MP_OPACITY)
    assert isinstance(color, ue.MaterialExpressionConstant3Vector)
    assert isinstance(opacity, ue.MaterialExpressionMultiply)
    color.set_editor_property("constant", ue.LinearColor(10.0, 4.0, 0.85, 1.0))
    opacity.set_editor_property("const_b", 0.8)
    materials.recompile_material(flash)
    assets.set_metadata_tag(flash, "ERWarmMuzzleFlash", "1")
    assert assets.save_loaded_asset(flash, only_if_is_dirty=False)
    assert all(digest(file) == before for file, before in hashes.items())
    ue.log(f"ER_MUZZLE_MATERIAL_SAVED protected={len(hashes)}")
