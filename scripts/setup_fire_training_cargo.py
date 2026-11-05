"""Bind matte, ribbed Twinmotion sheet to the Fire Training cargo role."""
from pathlib import Path
import zipfile

import unreal as ue


def tune_cargo(material, save):
    assets = ue.EditorAssetLibrary
    edit = ue.MaterialEditingLibrary
    folder = '/Game/EclipseRecoil/Art/SharedMaterials'
    name = 'M_ER_FireCargoSheet'
    path = folder + '/' + name
    master = assets.load_asset(path) if assets.does_asset_exist(path) else None
    if not master:
        master = ue.AssetToolsHelpers.get_asset_tools().create_asset(name, folder, ue.Material, ue.MaterialFactoryNew())
        if not master:
            raise RuntimeError('Cannot create cargo material')

        def node(kind, x, y):
            return edit.create_material_expression(master, kind, x, y)

        def wire(source, target, pin, output=''):
            if not edit.connect_material_expressions(source, output, target, pin):
                raise RuntimeError('Cannot wire cargo material input ' + pin)

        uv = node(ue.MaterialExpressionTextureCoordinate, -900, 0)
        params = {'UV': uv}
        for index, label in enumerate(['PanelWidth', 'PanelHeight', 'PaintIndex']):
            scalar = node(ue.MaterialExpressionScalarParameter, -900, 140 + index * 120)
            scalar.set_editor_property('parameter_name', label)
            scalar.set_editor_property('use_custom_primitive_data', True)
            scalar.set_editor_property('primitive_data_index', index)
            params[label] = scalar

        texture = node(ue.MaterialExpressionTextureSample, -600, 480)
        sheet = edit.get_material_instance_texture_parameter_value(material, 'Color Map')
        if not sheet:
            raise RuntimeError('Missing Twinmotion cargo sheet texture')
        texture.set_editor_property('texture', sheet)
        wire(uv, texture, 'UVs')
        params['Sheet'] = (texture, 'RGB')

        def custom(code, output_type, y):
            expression = node(ue.MaterialExpressionCustom, -250, y)
            expression.set_editor_property('code', code)
            expression.set_editor_property('output_type', output_type)
            inputs = []
            for label in params:
                entry = ue.CustomInput()
                entry.set_editor_property('input_name', label)
                inputs.append(entry)
            expression.set_editor_property('inputs', inputs)
            for label, source in params.items():
                if isinstance(source, tuple):
                    wire(source[0], expression, label, source[1])
                else:
                    wire(source, expression, label)
            return expression

        # The large cell faces use local UVs in metres, with height on U.
        # Border paint is part of the same surface so it disappears with the cells.
        profile = """
float2 p = abs(UV.yx) * 100;
float2 edge = min(p, float2(PanelWidth, PanelHeight) - p);
float frame = (PanelWidth > 0 && PanelHeight > 0)
    ? 1 - smoothstep(7, 10, min(edge.x, edge.y)) : 0;
float phase = p.x * (6.2831853 / 23);
float rib = cos(phase);
"""
        color = custom(profile + """
float3 paint = PaintIndex < .5 ? float3(.36,.07,.045)
    : PaintIndex < 1.5 ? float3(.055,.21,.13) : float3(.39,.24,.055);
float grain = lerp(.8, 1.15, dot(Sheet, float3(.2126,.7152,.0722)));
return paint * lerp((.78 + .16 * rib) * grain, 1.25, frame);
""", ue.CustomMaterialOutputType.CMOT_FLOAT3, 0)
        normal = custom(profile + """
return normalize(float3(0, sin(phase) * .38 * (1-frame), 1));
""", ue.CustomMaterialOutputType.CMOT_FLOAT3, 340)
        roughness = node(ue.MaterialExpressionConstant, -250, 650)
        roughness.set_editor_property('r', .83)
        for expression, target in [(color, ue.MaterialProperty.MP_BASE_COLOR),
                                   (normal, ue.MaterialProperty.MP_NORMAL),
                                   (roughness, ue.MaterialProperty.MP_ROUGHNESS)]:
            if not edit.connect_material_property(expression, '', target):
                raise RuntimeError('Cannot wire cargo output')
        edit.recompile_material(master)
        save(master)

    if material.get_editor_property('parent') != master:
        edit.set_material_instance_parent(material, master)
        edit.clear_all_material_instance_parameters(material)
        edit.update_material_instance(material)
        save(material)
        return True
    return False


def main():
    root = Path(__file__).resolve().parents[1]
    assets = ue.EditorAssetLibrary
    recipe = assets.load_asset('/Game/EclipseRecoil/Art/MapTemplates/DA_FireTraining')
    library = recipe.get_editor_property('theme_library')
    catalog = library.get_editor_property('shared_surfaces')
    cargo, = [finish for finish in catalog.get_editor_property('surfaces')
              if finish.get_name() == 'MI_AluminiumSiding02']
    backup = root / 'Saved/SharedMaterialLibraryBackup/before-fire-training-cargo-frame.zip'
    backup.parent.mkdir(parents=True, exist_ok=True)
    if not backup.exists():
        with zipfile.ZipFile(backup, 'w', zipfile.ZIP_DEFLATED) as archive:
            for asset in [library, cargo]:
                package = asset.get_path_name().split('.')[0]
                path = 'Content/' + package.removeprefix('/Game/') + '.uasset'
                archive.write(root / path, path)

    def save(asset):
        if not assets.save_loaded_asset(asset, False):
            raise RuntimeError('Could not save ' + asset.get_path_name())

    themes = list(library.get_editor_property('themes'))
    if any(theme.get_editor_property('cargo_steel') != cargo for theme in themes):
        for theme in themes:
            theme.set_editor_property('cargo_steel', cargo)
        library.set_editor_property('themes', themes)
        library.set_editor_property('revision', library.get_editor_property('revision') + 1)
        if not recipe.validate_layout(42):
            raise RuntimeError('Fire Training layout validation failed')
        tune_cargo(cargo, save)
        save(library)
    elif tune_cargo(cargo, save):
        library.set_editor_property('revision', library.get_editor_property('revision') + 1)
        save(library)
    print('ER_FIRE_TRAINING_CARGO_PASS')


if __name__ == '__main__':
    main()
