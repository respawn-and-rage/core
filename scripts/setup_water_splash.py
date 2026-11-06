"""Build the owned Niagara splash for bullets crossing a pool surface."""
import unreal as ue

root = '/Game/EclipseRecoil/Effects/Shots'
tools = ue.AssetToolsHelpers.get_asset_tools()
edit = ue.MaterialEditingLibrary
assets = ue.EditorAssetLibrary
mat = ue.load_asset(root + '/M_ER_WaterSplash') or tools.create_asset(
    'M_ER_WaterSplash', root, ue.Material, ue.MaterialFactoryNew())
edit.delete_all_material_expressions(mat)
mat.set_editor_property('shading_model', ue.MaterialShadingModel.MSM_UNLIT)
mat.set_editor_property('blend_mode', ue.BlendMode.BLEND_ADDITIVE)
mat.set_editor_property('used_with_niagara_sprites', True)
color = edit.create_material_expression(mat, ue.MaterialExpressionConstant3Vector)
color.set_editor_property('constant', ue.LinearColor(.25, .65, 1.2, 1))
assert edit.connect_material_property(color, '', ue.MaterialProperty.MP_EMISSIVE_COLOR)
uv = edit.create_material_expression(mat, ue.MaterialExpressionTextureCoordinate)
soft = edit.create_material_expression(mat, ue.MaterialExpressionCustom)
soft.set_editor_property('output_type', ue.CustomMaterialOutputType.CMOT_FLOAT1)
soft.set_editor_property('code', 'float2 p=UV*2-1; return .55*pow(saturate(1-length(p)),2);')
arg = ue.CustomInput()
arg.set_editor_property('input_name', 'UV')
soft.set_editor_property('inputs', [arg])
assert edit.connect_material_expressions(uv, '', soft, 'UV')
assert edit.connect_material_property(soft, '', ue.MaterialProperty.MP_OPACITY)
edit.recompile_material(mat)
assert assets.save_loaded_asset(mat, False)
system = ue.load_asset(root + '/NS_ER_WaterSplash') or tools.create_asset(
    'NS_ER_WaterSplash', root, ue.NiagaraSystem, ue.NiagaraSystemFactoryNew())
assert ue.ERShotEffects.configure_impact(system, mat)
assert assets.save_loaded_asset(system, False)
ue.log('ER_WATER_SPLASH_READY')
