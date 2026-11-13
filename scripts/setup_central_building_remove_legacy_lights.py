"""Remove Central Building's old unshadowed point lights through Unreal."""
from hashlib import sha256
from pathlib import Path
import shutil

import unreal as ue


def digest(path):
    return sha256(path.read_bytes()).hexdigest()


def main():
    root = Path(__file__).resolve().parents[1]
    content = root / 'Assets/maps/Content/EclipseRecoil'
    path = '/Game/EclipseRecoil/Maps/L_CentralBuilding'
    package = content / 'Maps/L_CentralBuilding.umap'
    protected = {file: digest(file) for file in [*content.glob('Maps/*.umap'),
                 *content.glob('Art/MapTemplates/*.uasset')] if file != package}
    levels = ue.get_editor_subsystem(ue.LevelEditorSubsystem)
    actors = ue.get_editor_subsystem(ue.EditorActorSubsystem)
    legacy_labels = {'Industrial_Interior_-700', 'Industrial_Interior_700'}

    def inventory():
        return {actor.get_actor_label(): actor for actor in actors.get_all_level_actors()
                if isinstance(actor, (ue.PointLight, ue.DirectionalLight, ue.SkyLight))}

    def settings(lights):
        result = {}
        for label, light in lights.items():
            if label in legacy_labels:
                continue
            position, rotation = light.get_actor_location(), light.get_actor_rotation()
            result[label] = (light.get_class().get_name(), position.x, position.y, position.z,
                             rotation.pitch, rotation.yaw, rotation.roll,
                             light.light_component.get_editor_property('intensity'))
        return result

    if not levels.load_level(path):
        raise RuntimeError('Cannot load Central Building')
    lights = inventory()
    remaining = settings(lights)
    if not (any(isinstance(light, ue.DirectionalLight) for light in lights.values())
            and any(isinstance(light, ue.SkyLight) for light in lights.values())):
        raise RuntimeError('Central Building must retain its sun and skylight')
    legacy = sorted(legacy_labels & lights.keys())
    if any(not isinstance(lights[label], ue.PointLight) for label in legacy):
        raise RuntimeError('Unexpected actor using a legacy light label')
    if legacy:
        before = digest(package)
        backup = root / 'Saved/Backups/CentralLegacyLights' / f'L_CentralBuilding-{before}.umap'
        backup.parent.mkdir(parents=True, exist_ok=True)
        if not backup.exists():
            shutil.copy2(package, backup)
        if digest(backup) != before:
            raise RuntimeError('Invalid Central Building backup')
        for label in legacy:
            light = lights[label]
            ue.log(f'ER_CENTRAL_LEGACY_LIGHT label={label} pos={light.get_actor_location()} '
                   f'casts_shadows={light.light_component.get_editor_property("cast_shadows")}')
            if not actors.destroy_actor(light):
                raise RuntimeError(f'Cannot remove {label}')
        if not levels.save_current_level() or not levels.load_level(path):
            raise RuntimeError('Cannot save and reopen Central Building')
    current = inventory()
    if legacy_labels & current.keys() or settings(current) != remaining:
        raise RuntimeError('Unexpected light inventory after migration')
    if any(digest(file) != previous for file, previous in protected.items()):
        raise RuntimeError('A protected map or template changed')
    ue.log(f'ER_CENTRAL_LEGACY_LIGHTS_OK removed={len(legacy)} '
           f'authored_points={sum(isinstance(light, ue.PointLight) for light in current.values())}')


main()
