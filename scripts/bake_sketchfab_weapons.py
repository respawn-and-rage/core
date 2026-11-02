"""Convert supplied FBX rigid-object animation to Unreal skeletal animation.

Run with Blender --background --python THIS -- private/SketchfabWeapons.
Original geometry, UVs and animation files remain unchanged. Derived outputs
omit source arms; the game keeps its fitted skin forearms. Animation samples
and source wrist motion are preserved in the private manifest for retargeting.
"""
from pathlib import Path
import json
import math
import re
import sys
import bpy
from mathutils import Matrix, Vector

root = Path(sys.argv[sys.argv.index('--') + 1]).resolve()
only = sys.argv[sys.argv.index('--') + 2:]
models = [
    ('Knife', 'f3140', 'knife', 1, dict(Idle=(1, 2), Fire=(1, 25), Source=(1, 146))),
    ('Pistol', 'bd896', 'pistol', 1, dict(Idle=(1, 2), Fire=(1, 12), Reload=(13, 83), Source=(1, 265))),
    ('SMG', 'ca37', None, 1, dict(Idle='KDW_DPose_Idle', Fire='KDW_Shot', Reload='KDW_Reload_full')),
    ('Shotgun', 'c3d3', 'shotgun', 1, dict(Idle=(1, 2), Fire=(1, 13), Reload=(14, 103), Source=(1, 141))),
    ('Assault', '924e', None, .85 / 11.481, dict(Idle='idle', Fire='shooting', Reload='reload')),
    ('Carbine', '62977', 'carbine', 1, dict(Idle=(1, 2), Fire=(1, 7), Reload=(8, 168), Source=(1, 223))),
    ('Sniper', 'eae1', 'sniper', 1, dict(Idle=(1, 2), Fire=(1, 61), Reload=(62, 116), Source=(1, 196))),
    ('Launcher', 'c1ec', 'GL_base', 1, dict(Idle=(2, 3), Fire=(2, 13), Reload=(14, 212), Source=(2, 321))),
    ('Projectile', 'c1ec', 'GL_slug1', 1, dict(Idle=(2, 3))),
    ('HE', '287306', 'F1', 1, dict(Idle=(1, 2))),
    ('Smoke', '287306', 'M18', 1, dict(Idle=(1, 2))),
]

def rigid_matrix(matrix):
    position, rotation, _ = matrix.decompose()
    return Matrix.LocRotScale(position, rotation, Vector((1, 1, 1)))

def clean(name):
    return re.sub('[^a-zA-Z0-9_]', '_', name)

report = []
for key, prefix, gun_parent, scale, clips in models:
    if only and key not in only:
        continue
    directory = next(root.glob(prefix + '*'))
    sources = sorted(directory.glob('original/**/*.fbx'))
    if not sources:
        print('ER_BAKE_WAIT', key, flush=True)
        continue
    bpy.ops.wm.read_factory_settings(use_empty=True)
    bpy.ops.import_scene.fbx(filepath=str(sources[0]))
    scene = bpy.context.scene
    source_fps = scene.render.fps / scene.render.fps_base
    original_objects = list(bpy.data.objects)
    rigs = [o for o in original_objects if o.type == 'ARMATURE']
    gun_objects = [o for o in original_objects if o.type == 'MESH' and 'arm' not in o.name.lower()]
    if key == 'SMG':
        # Pmag is the inserted magazine; Pmag_2 is the loose reload prop.
        # The source also includes an unskinned helper cube.
        gun_objects = [o for o in gun_objects if o.vertex_groups and
                       {g.name for g in o.vertex_groups} != {'Pmag_2'}]
        assert len(gun_objects) == 1 and 'Pmag' in gun_objects[0].vertex_groups, \
            'SMG source changed: inspect the inserted and spare magazines before baking'
    if key == 'Launcher':
        gun_objects = [o for o in gun_objects if o.name != 'GL_scope']
    if key in ('HE', 'Smoke', 'Projectile'):
        gun_objects = [bpy.data.objects[gun_parent]]
    if isinstance(clips['Idle'], str):
        rig = rigs[0]
        idle_action = next(a for a in bpy.data.actions if a.name.split('|')[-1] == clips['Idle'] and a.name.startswith(rig.name + '|'))
        rig.animation_data.action = idle_action
    scene.frame_set(int(idle_action.frame_range[0]) if isinstance(clips['Idle'],str) else (2 if key in ('Launcher','Projectile') else 1))
    bpy.context.view_layer.update()
    # Source guns face -Y with Z up. Normalize the owned derivative to +X.
    orient = Matrix.Rotation(-math.pi / 2 if key=='Assault' else math.pi / 2, 4, 'Z')
    if key in ('HE', 'Smoke', 'Projectile'):
        obj=gun_objects[0]
        anchor=sum((obj.matrix_world @ Vector(c) for c in obj.bound_box),Vector())/8
        if key=='HE':anchor.z-=.03
    elif gun_parent:
        anchor = bpy.data.objects[gun_parent].matrix_world.translation.copy()
        trigger=bpy.data.objects.get('trigger') or bpy.data.objects.get('GL_trigger')
        if key != 'Knife' and trigger:
            anchor = trigger.matrix_world.translation + Vector((0,.03,-.04))
    else:
        # The AR15 uses arbitrary source units. The SMG has a pistol and SMG
        # rig in one file; choose its actual rifle bone, not the pistol pose.
        if key == 'SMG':
            anchor = (rigs[0].matrix_world @ rigs[0].pose.bones['Trigger'].matrix).translation + Vector((0,.03,-.04))
        else:
            anchor = (rigs[0].matrix_world @ rigs[0].pose.bones[0].matrix).translation.copy()
    normalize = Matrix.Scale(scale, 4) @ orient @ Matrix.Translation(-anchor)
    # The AR source root is beside the trigger, not in the pistol grip.
    if key == 'Assault':
        normalize = Matrix.Translation((.065, 0, .035)) @ normalize
    # Grip centres in the normalized, full-size weapon frame (metres).
    support = Vector(dict(Knife=(.06,0,0), Pistol=(-.02,0,-.025), SMG=(.19,0,-.015),
        Shotgun=(.40,0,-.035), Assault=(.345,0,.065), Carbine=(.28,0,.015),
        Sniper=(.25,0,-.015), Launcher=(.30,0,-.06), HE=(0,0,0), Smoke=(0,0,0),
        Projectile=(0,0,0))[key])
    def source_root():
        if gun_parent:return bpy.data.objects[gun_parent].matrix_world.copy()
        rig=rigs[0]
        bone='Rif' if key=='SMG' else ('Bone' if key=='Assault' else rig.pose.bones[0].name)
        return rig.matrix_world @ rig.pose.bones[bone].matrix
    root_idle=rigid_matrix(normalize @ source_root())
    wrists={}
    for rig in rigs:
        for side,names in [('l',['L_wrist','Hand_L']),('r',['R_wrist','Hand_R'])]:
            bone=next((rig.pose.bones.get(n) for n in names if rig.pose.bones.get(n)),None)
            if bone:wrists[side]=(rig,bone.name,rigid_matrix(normalize @ rig.matrix_world @ bone.matrix))
    entries = []
    for rig in rigs:
        used = {g.name for obj in gun_objects if any(m.type == 'ARMATURE' and m.object == rig for m in obj.modifiers) for g in obj.vertex_groups}
        for bone in rig.data.bones:
            if bone.name in used:
                entries.append(dict(name='B_' + clean(bone.name), rig=rig, bone=bone.name,
                                    source_scale=(normalize @ rig.matrix_world @ bone.matrix_local).to_scale(),
                                    rest=rigid_matrix(normalize @ rig.matrix_world @ bone.matrix_local)))
    for obj in gun_objects:
        if not any(m.type == 'ARMATURE' for m in obj.modifiers):
            entries.append(dict(name='P_' + clean(obj.name), obj=obj,
                                source_scale=(normalize @ obj.matrix_world).to_scale(),
                                rest=rigid_matrix(normalize @ obj.matrix_world)))
    armature = bpy.data.armatures.new('ER_' + key)
    baked = bpy.data.objects.new('ER_' + key, armature)
    scene.collection.objects.link(baked)
    bpy.context.view_layer.objects.active = baked
    baked.select_set(True)
    bpy.ops.object.mode_set(mode='EDIT')
    base = armature.edit_bones.new('root'); base.head=(0,0,0); base.tail=(0,.01,0)
    for entry in entries:
        bone = armature.edit_bones.new(entry['name']); bone.head=(0,0,0); bone.tail=(0,.01,0)
        bone.matrix = entry['rest']; bone.parent=base
    bpy.ops.object.mode_set(mode='OBJECT')
    outputs = []
    for obj in gun_objects:
        duplicate = bpy.data.objects.new(clean(obj.name), obj.data.copy())
        scene.collection.objects.link(duplicate)
        duplicate.data.transform(normalize @ obj.matrix_world)
        if key in ('Launcher','Projectile'):
            # The source mixes DiffuseUV and UVMap by material. Interchange can
            # select the empty layer on the cylinder. Consolidate the populated
            # layer for each material into a single, unambiguous UV0.
            layers=list(duplicate.data.uv_layers)
            scores={}
            for poly in duplicate.data.polygons:
                values=scores.setdefault(poly.material_index,[0.]*len(layers))
                for i,layer in enumerate(layers):
                    uv=[layer.data[j].uv for j in poly.loop_indices]
                    values[i]+=sum((v-uv[0]).length for v in uv[1:])
            selected={m:max(range(len(v)),key=lambda i:v[i]) for m,v in scores.items()}
            coords={j:layers[selected[p.material_index]].data[j].uv.copy()
                    for p in duplicate.data.polygons for j in p.loop_indices}
            for layer in layers:duplicate.data.uv_layers.remove(layer)
            uv=duplicate.data.uv_layers.new(name='WeaponUV')
            for j,value in coords.items():uv.data[j].uv=value
        for material in duplicate.data.materials:
            if material and not material.name.startswith(key + '_'):
                material.name = key + '_' + clean(material.name)
        weights = {g.index:g.name for g in obj.vertex_groups}
        skinned = any(m.type == 'ARMATURE' for m in obj.modifiers)
        if skinned:
            groups = {name:duplicate.vertex_groups.new(name='B_' + clean(name)) for name in weights.values()}
            for vertex in obj.data.vertices:
                for group in vertex.groups:
                    groups[weights[group.group]].add([vertex.index],group.weight,'REPLACE')
        else:
            group=duplicate.vertex_groups.new(name='P_' + clean(obj.name))
            group.add(list(range(len(duplicate.data.vertices))),1,'REPLACE')
        modifier=duplicate.modifiers.new('Source animation','ARMATURE');modifier.object=baked
        duplicate.parent=baked
        outputs.append(duplicate)
    # Samples are captured before writing derivative actions, so source actions
    # and source rigs keep their original data throughout evaluation.
    samples = {};motions={}
    for label, interval in clips.items():
        if isinstance(interval, str):
            rig=rigs[0]
            action=next(a for a in bpy.data.actions if a.name.split('|')[-1]==interval and a.name.startswith(rig.name+'|'))
            rig.animation_data.action=action
            start,end=map(int,action.frame_range)
        else:
            start,end=interval
        clip=[];motion_clip=[]
        for frame in range(start,end+1):
            scene.frame_set(frame);bpy.context.view_layer.update()
            motion=rigid_matrix(normalize @ source_root()) @ root_idle.inverted()
            transforms={}
            for entry in entries:
                source_matrix = entry['rig'].matrix_world @ entry['rig'].pose.bones[entry['bone']].matrix if 'rig' in entry else entry['obj'].matrix_world
                normalized=normalize @ source_matrix
                position,rotation,current_scale=normalized.decompose()
                relative_scale=Vector(tuple(current_scale[i]/entry['source_scale'][i] if abs(entry['source_scale'][i])>1e-8 else 1 for i in range(3)))
                transforms[entry['name']]=motion.inverted() @ Matrix.LocRotScale(position,rotation,relative_scale)
            clip.append(transforms)
            hands={}
            for side,(rig,bone,idle) in wrists.items():
                current=rigid_matrix(normalize @ rig.matrix_world @ rig.pose.bones[bone].matrix)
                delta=current @ idle.inverted()
                # Rotate the wrist-to-palm offset with the source wrist. A
                # translation delta alone slides the palm off a rotating mag.
                contact = support if side=='l' else Vector((0,0,0))
                palm=current @ idle.inverted() @ contact
                hands[side]=dict(delta=[list(row) for row in delta],position=list(current.translation-idle.translation),grip=list(palm))
            if key=='Assault' and label=='Reload':
                # This source has gun bones but no hands. Follow its actual
                # detachable magazine while held, with approach/release blends.
                mag=transforms['B_Bone_004']
                if not clip[:-1]:mag_idle=mag.copy()
                t=(frame-start)/max(1,end-start)
                weight=min(1,max(0,(t-.08)/.12),max(0,(.90-t)/.15))
                contact=mag @ mag_idle.inverted() @ Vector((.155,0,-.045))
                palm=motion @ support.lerp(contact,weight)
                hands['l']=dict(delta=[list(row) for row in motion],grip=list(palm),position=list(palm-support))
            motion_clip.append(dict(root=[list(row) for row in motion],hands=hands))
        if label=='Idle':
            # A ready pose must never loop the first two firing frames.
            clip=[clip[0],clip[0]];motion_clip=[motion_clip[0],motion_clip[0]]
        samples[label]=clip
        motions[label]=motion_clip
    baked.animation_data_create()
    if round(source_fps)!=30:
        # Interchange's default 30Hz grid must have an integral key count.
        # Use the exact same nearest-frame resampling as the fitted hand rig.
        for label,clip in samples.items():
            last=max(1,round((len(clip)-1)*30/source_fps))
            samples[label]=[clip[min(len(clip)-1,round(i*source_fps/30))] for i in range(last+1)]
    scene.render.fps=30;scene.render.fps_base=1
    actions=[]
    for label, clip in samples.items():
        action=bpy.data.actions.new(key+'_'+label);baked.animation_data.action=action
        for frame, transforms in enumerate(clip,1):
            for name, matrix in transforms.items():
                bone=baked.pose.bones[name];bone.matrix=matrix;bone.rotation_mode='QUATERNION'
                for channel in ['location','rotation_quaternion','scale']:
                    bone.keyframe_insert(data_path=channel,frame=frame,group=name)
        action.use_fake_user=True;actions.append(action)
    # Export only derivative actions. The originals remain in the archive.
    for obj in original_objects:
        bpy.data.objects.remove(obj,do_unlink=True)
    for action in list(bpy.data.actions):
        if action not in actions:bpy.data.actions.remove(action)
    baked.animation_data.action=actions[0]
    scene.frame_start=1;scene.frame_end=2;scene.frame_set(1)
    bpy.ops.object.select_all(action='DESELECT')
    baked.select_set(True)
    for obj in outputs:obj.select_set(True)
    destination=directory/'derived';destination.mkdir(exist_ok=True)
    (destination/('motion-'+key+'.json')).write_text(json.dumps(dict(fps=source_fps,support=list(support),clips=motions))+'\n')
    bpy.ops.export_scene.fbx(filepath=str(destination/('SK_'+key+'.fbx')),use_selection=True,
        object_types={'MESH','ARMATURE'},add_leaf_bones=False,bake_anim=True,
        bake_anim_use_all_actions=True,bake_anim_use_nla_strips=False,bake_anim_simplify_factor=0,
        bake_anim_step=1,axis_forward='-Y',axis_up='Z',mesh_smooth_type='FACE')
    item=dict(key=key,source=str(sources[0].relative_to(root)),fbx=str((destination/('SK_'+key+'.fbx')).relative_to(root)),
              fps=source_fps,clips={label:len(clip) for label,clip in samples.items()},
              vertices=sum(len(o.data.vertices) for o in outputs),bones=len(entries)+1,
              materials=sorted({m.name for o in outputs for m in o.data.materials if m}))
    (destination/('manifest-'+key+'.json')).write_text(json.dumps(item,indent=2)+'\n')
    print('ER_BAKE_COMPLETE',json.dumps(item),flush=True)
    report.append(item)
all_reports=[json.loads(path.read_text()) for path in sorted(root.glob('*/derived/manifest-*.json'))]
(root/'bake-report.json').write_text(json.dumps(sorted(all_reports,key=lambda item:item['key']),indent=2)+'\n')
