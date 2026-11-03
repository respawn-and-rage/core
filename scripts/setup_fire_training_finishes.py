"""Unreal-only Fire Training finishes: neutral steel ceiling and concrete walls.

Create an owned world-space steel material and update only the Fire Training palette.
Back up the palette and verify all other recipes, palettes, levels and source materials.
"""
import hashlib,json,shutil
from pathlib import Path
import unreal as ue
root=Path(ue.Paths.project_dir()).resolve();a=ue.EditorAssetLibrary;e=ue.MaterialEditingLibrary;tools=ue.AssetToolsHelpers.get_asset_tools()
art='/Game/EclipseRecoil/Art/';path=art+'MapTemplates/DA_Theme_FireTraining';folder=art+'FireTraining/Procedural'
material_path=folder+'/M_ER_FireTraining_CeilingSteel'
lib=a.load_asset(path)
if not lib:raise RuntimeError('Missing Fire Training palette')
if a.get_metadata_tag(lib,'ERFireTrainingFinishesRevision')=='1':
    print('ER_FIRE_FINISHES already migrated')
else:
    disk=root/'Assets/maps/Content/EclipseRecoil';file=disk/'Art/MapTemplates/DA_Theme_FireTraining.uasset'
    sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
    protected=[p for p in (disk/'Art/MapTemplates').glob('*.uasset') if p!=file]+list((disk/'Maps').glob('*.umap'))
    sources=[disk/'Art/Industrial/SketchfabTextures/M_ER_SF_concrete_seamless.uasset',disk/'Art/Industrial/Materials/M_ER_FactoryGraySteel.uasset']
    protected+=sources;before={str(p):sha(p) for p in protected}
    revision=lib.get_editor_property('revision');backup=root/'Saved/FireTrainingBackup'/('before-finishes-revision'+str(revision));backup.mkdir(parents=True,exist_ok=True)
    copy=backup/file.name
    if not copy.exists():shutil.copy2(file,copy)
    if sha(copy)!=sha(file):raise RuntimeError('Palette rollback mismatch')
    a.make_directory(folder)
    if a.does_asset_exist(material_path):
        steel=a.load_asset(material_path)
        if a.get_metadata_tag(steel,'ERFireTrainingFinishOwned')!='1':raise RuntimeError('Unowned material '+material_path)
    else:
        steel=tools.create_asset('M_ER_FireTraining_CeilingSteel',folder,ue.Material,ue.MaterialFactoryNew())
        steel.set_editor_property('used_with_instanced_static_meshes',True)
        world=e.create_material_expression(steel,ue.MaterialExpressionWorldPosition,-500,0)
        color=e.create_material_expression(steel,ue.MaterialExpressionCustom,-200,0)
        color.set_editor_property('output_type',ue.CustomMaterialOutputType.CMOT_FLOAT3)
        color.set_editor_property('code','float2 q=frac(P.xy/float2(200.0,400.0)); float2 d=min(q,1.0-q); float seam=1.0-smoothstep(0.002,0.006,min(d.x,d.y)); return lerp(float3(0.55,0.55,0.55),float3(0.16,0.16,0.16),seam);')
        entry=ue.CustomInput();entry.set_editor_property('input_name','P');color.set_editor_property('inputs',[entry])
        if not e.connect_material_expressions(world,'',color,'P'):raise RuntimeError('Cannot connect world-space steel')
        if not e.connect_material_property(color,'',ue.MaterialProperty.MP_BASE_COLOR):raise RuntimeError('Cannot connect ceiling color')
        for prop,value,y in [(ue.MaterialProperty.MP_METALLIC,.75,180),(ue.MaterialProperty.MP_ROUGHNESS,.5,280)]:
            n=e.create_material_expression(steel,ue.MaterialExpressionConstant,-200,y);n.set_editor_property('r',value)
            if not e.connect_material_property(n,'',prop):raise RuntimeError('Cannot connect steel property')
        a.set_metadata_tag(steel,'ERFireTrainingFinishOwned','1');e.recompile_material(steel)
        if not a.save_loaded_asset(steel,False):raise RuntimeError('Cannot save steel ceiling')
    concrete=a.load_asset(art+'Industrial/SketchfabTextures/M_ER_SF_concrete_seamless')
    if not concrete:raise RuntimeError('Missing concrete finish')
    themes=list(lib.get_editor_property('themes'))
    for theme in themes:
        theme.set_editor_property('ceiling',steel);theme.set_editor_property('wall',concrete)
        rows=list(theme.get_editor_property('materials'));rows[2]=concrete;theme.set_editor_property('materials',rows)
    lib.set_editor_property('themes',themes);lib.set_editor_property('revision',revision+1)
    a.set_metadata_tag(lib,'ERFireTrainingFinishesRevision','1')
    if not a.save_loaded_asset(lib,False):raise RuntimeError('Cannot save Fire Training palette')
    for p,digest in before.items():
        if sha(Path(p))!=digest:raise RuntimeError('Protected asset changed '+p)
    (backup/'receipt.json').write_text(json.dumps(dict(before_revision=revision,after_revision=revision+1,before_sha256=sha(copy),after_sha256=sha(file),steel=material_path,concrete=concrete.get_path_name(),protected_assets=len(before)),indent=2)+'\n')
    print('ER_FIRE_FINISHES_SAVED protected_assets='+str(len(before)))
