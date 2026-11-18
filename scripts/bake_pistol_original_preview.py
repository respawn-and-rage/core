"""Bake a source-faithful combined arms/weapon preview, separate from gameplay."""
from pathlib import Path
import json,sys
import bpy
from mathutils import Matrix,Vector
root=Path(sys.argv[sys.argv.index('--')+1]).resolve()
src=root/'original/source/arms@beretta.fbx';out=root/'original-preview';out.mkdir(exist_ok=True)
bpy.ops.wm.read_factory_settings(use_empty=True);bpy.ops.import_scene.fbx(filepath=str(src))
scene=bpy.context.scene;scene.frame_set(1);bpy.context.view_layer.update()
originals=list(bpy.data.objects);rig=next(o for o in originals if o.type=='ARMATURE')
meshes=[o for o in originals if o.type=='MESH']
normalize=Matrix.Rotation(3.141592653589793/2,4,'Z')
def rigid(m):
 p,q,_=m.decompose();return Matrix.LocRotScale(p,q,Vector((1,1,1)))
entries=[]
for b in rig.data.bones:
 m=normalize@rig.matrix_world@b.matrix_local
 entries.append(dict(name='B_'+b.name,rig=rig,bone=b.name,rest=rigid(m),scale=m.to_scale()))
for obj in meshes:
 if not any(m.type=='ARMATURE' for m in obj.modifiers):
  m=normalize@obj.matrix_world
  entries.append(dict(name='P_'+obj.name,obj=obj,rest=rigid(m),scale=m.to_scale()))
# Capture the source before creating any derivative actions.
samples=[]
for frame in range(1,266):
 scene.frame_set(frame);bpy.context.view_layer.update();row={}
 for e in entries:
  m=normalize@(e['rig'].matrix_world@e['rig'].pose.bones[e['bone']].matrix if 'rig' in e else e['obj'].matrix_world)
  p,q,s=m.decompose();s=Vector(tuple(s[i]/e['scale'][i] if abs(e['scale'][i])>1e-9 else 1 for i in range(3)))
  row[e['name']]=Matrix.LocRotScale(p,q,s)
 samples.append(row)
scene.frame_set(1)
arm=bpy.data.armatures.new('PistolOriginal');baked=bpy.data.objects.new('PistolOriginal',arm);scene.collection.objects.link(baked)
bpy.context.view_layer.objects.active=baked;baked.select_set(True);bpy.ops.object.mode_set(mode='EDIT')
b=arm.edit_bones.new('root');b.head=(0,0,0);b.tail=(0,.01,0)
for e in entries:
 b=arm.edit_bones.new(e['name']);b.head=(0,0,0);b.tail=(0,.01,0);b.matrix=e['rest'];b.parent=arm.edit_bones['root']
bpy.ops.object.mode_set(mode='OBJECT');outputs=[]
for obj in meshes:
 new=bpy.data.objects.new('Original_'+obj.name,obj.data.copy());scene.collection.objects.link(new)
 new.data.transform(normalize@obj.matrix_world)
 if any(m.type=='ARMATURE' for m in obj.modifiers):
  groups={g.index:new.vertex_groups.new(name='B_'+g.name) for g in obj.vertex_groups}
  for v in obj.data.vertices:
   for g in v.groups:groups[g.group].add([v.index],g.weight,'REPLACE')
 else:
  g=new.vertex_groups.new(name='P_'+obj.name);g.add(list(range(len(new.data.vertices))),1,'REPLACE')
 mod=new.modifiers.new('Original source motion','ARMATURE');mod.object=baked;new.parent=baked;outputs.append((obj,new))
actions=[]
for label,start,end in [('Full',1,265),('Reload',13,83)]:
 action=bpy.data.actions.new('Original_'+label);action.use_fake_user=True;baked.animation_data_create();baked.animation_data.action=action
 for i,frame in enumerate(range(start,end+1),1):
  for name,m in samples[frame-1].items():
   b=baked.pose.bones[name];b.rotation_mode='QUATERNION';b.matrix=m
   b.keyframe_insert('location',frame=i);b.keyframe_insert('rotation_quaternion',frame=i);b.keyframe_insert('scale',frame=i)
 actions.append(action)
# Validate evaluated vertices against the original source at representative times.
baked.animation_data.action=actions[0];max_error=0.
for frame in (1,13,30,60,83,150,265):
 scene.frame_set(frame);bpy.context.view_layer.update();deps=bpy.context.evaluated_depsgraph_get()
 for original,new in outputs:
  a=original.evaluated_get(deps);b=new.evaluated_get(deps)
  ma=a.to_mesh();mb=b.to_mesh()
  for va,vb in zip(ma.vertices,mb.vertices):max_error=max(max_error,((normalize@a.matrix_world@va.co)-(b.matrix_world@vb.co)).length)
  a.to_mesh_clear();b.to_mesh_clear()
if max_error>.0001:raise RuntimeError('Source preview deformation error: '+str(max_error))
for a in list(bpy.data.actions):
 if a not in actions:bpy.data.actions.remove(a)
baked.animation_data.action=actions[1];scene.frame_start=1;scene.frame_end=71;scene.frame_set(1)
bpy.ops.object.select_all(action='DESELECT');baked.select_set(True)
for _,obj in outputs:obj.select_set(True)
bpy.ops.export_scene.fbx(filepath=str(out/'PistolOriginal.fbx'),use_selection=True,object_types={'MESH','ARMATURE'},add_leaf_bones=False,bake_anim=True,bake_anim_use_all_actions=True,bake_anim_use_nla_strips=False,bake_anim_simplify_factor=0,bake_anim_step=1,axis_forward='-Y',axis_up='Z',mesh_smooth_type='FACE')
(out/'manifest.json').write_text(json.dumps(dict(fps=scene.render.fps,source_frames=[1,265],reload_frames=[13,83],bones=len(entries)+1,meshes=len(outputs),max_vertex_error_m=max_error),indent=2)+'\n')
print('ER_PISTOL_ORIGINAL_PREVIEW_BAKED',max_error,flush=True)
