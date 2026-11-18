"""Retarget the recovered source onto the shared FPS skin rig.

Pass -ERSetupPistolSourceIdle for the first source frame or
-ERSetupPistolSourceFire for its matching firing frames.

Only source motion and anatomical rig fitting are used: no camera pocket,
manual start, elapsed-time fades or fixed support elbow. Existing packages are
protected; only the new reload and its data reference are saved.
"""
from pathlib import Path
import hashlib,json,math,runpy,zipfile
import unreal as u
root=Path(u.Paths.project_dir()).resolve();assets=u.EditorAssetLibrary
h=runpy.run_path(str(root/'scripts/retarget_weapon_hands.py'))
xyz,quat,qmul,inv,rotate,add,sub,mul,dot,length,unit,from_to,tf=[h[n] for n in ('xyz','quat','qmul','inverse','rotate','add','sub','mul','dot','length','unit','from_to','transform')]
folder='/Game/EclipseRecoil/Weapons/Sketchfab/Pistol'
preview=folder+'/OriginalPreview'
data=u.load_asset(folder+'/DA_Pistol')
source_full=u.load_asset(preview+'/SK_PistolOriginalPistolOriginal_Original_Full')
source_reload=u.load_asset(preview+'/SK_PistolOriginalPistolOriginal_Original_Reload')
base=u.load_asset(folder+'/A_Pistol_Hands_Idle')
source_mesh=u.load_asset(preview+'/SK_PistolOriginal')
args = u.SystemLibrary.get_command_line().split()
idle_only = '-ERSetupPistolSourceIdle' in args
fire_only = '-ERSetupPistolSourceFire' in args
if idle_only and fire_only:
 raise RuntimeError('Choose one pistol animation to set up')
reload_only = not (idle_only or fire_only)
mode = 'Idle' if idle_only else 'Fire' if fire_only else 'Reload'
animation_field = mode.lower() + '_animation'
active_before = data.get_editor_property(animation_field)
path = folder + '/A_Pistol_Hands_Source' + mode
if not all((data,source_full,source_reload,base,source_mesh)):raise RuntimeError('Missing recovered source assets')

def file(a):return root/('Content/'+a.get_path_name().split('.')[0].removeprefix('/Game/')+'.uasset')
def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def save(a):
 if not assets.save_loaded_asset(a,False):raise RuntimeError('Cannot save '+a.get_path_name())
owned={file(data).resolve(),(root/('Content/'+path.removeprefix('/Game/')+'.uasset')).resolve()}
protected={p.resolve():sha(p) for subdir in ('characters','weapons','maps') for p in (root/'Assets'/subdir/'Content').rglob('*.uasset') if p.resolve() not in owned}
backup = root / 'Saved' / ('PistolSource' + mode)
backup.mkdir(parents=True, exist_ok=True)
reload_references = {field: data.get_editor_property(field) for field in ('reload_animation', 'weapon_reload_animation')}
original_reload_hashes = {file(clip): sha(file(clip)) for clip in reload_references.values()}
archive=backup/'before.zip'
if not archive.exists():
 with zipfile.ZipFile(archive,'x',zipfile.ZIP_DEFLATED) as z:
  for a in (data,active_before):z.write(file(a),str(file(a).relative_to(root)))
 with zipfile.ZipFile(archive) as z:
  if z.testzip() or any(z.read(str(file(a).relative_to(root))) != file(a).read_bytes() for a in (data, active_before)):
   raise RuntimeError('Invalid backup')
so=u.AnimPoseEvaluationOptions();so.should_retarget=False;so.evaluation_type=u.AnimDataEvalType.RAW;so.optional_skeletal_mesh=source_mesh
fo=u.AnimPoseEvaluationOptions();fo.should_retarget=False;fo.evaluation_type=u.AnimDataEvalType.RAW
fo.optional_skeletal_mesh=u.load_asset('/Game/EclipseRecoil/Characters/Teams/Alpha/FirstPerson/SK_ER_Belica_Alpha_BelicaArmor_Forearms')
go=u.AnimPoseEvaluationOptions();go.should_retarget=False;go.evaluation_type=u.AnimDataEvalType.RAW;go.optional_skeletal_mesh=data.mesh
world,local=u.AnimPoseSpaces.WORLD,u.AnimPoseSpaces.LOCAL
def evaluate(a,t=0,options=fo):return u.AnimPoseExtensions.get_anim_pose_at_time(a,t,options)
def bone(p,n,space=world):return u.AnimPoseExtensions.get_bone_pose(p,n,space)
def socket(p,n):return u.AnimPoseExtensions.get_socket_pose(p,n,world)
def compose(a,b):return u.MathLibrary.compose_transforms(a,b)
def inverse(a):return u.MathLibrary.invert_transform(a)
rest=evaluate(base);sr=evaluate(source_full,options=so)
gun_rest=evaluate(data.weapon_idle_animation,options=go)
frame=data.first_person_grip_transform
palm=socket(rest,'ER_Palm_R');hand=bone(rest,'hand_r')
socket_relative=u.MathLibrary.make_relative_transform(palm,hand)
weapon=compose(frame,palm);weapon.set_editor_property('scale3d',frame.scale3d)
target_base=compose(bone(gun_rest,'P_base'),weapon)
# One rigid source-to-FPS frame aligns the original pistol geometry.
mapping=compose(inverse(bone(sr,'P_base')),target_base)
mapping_q=quat(mapping.rotation)
def mapped(t):return compose(t,mapping)
def pos(t):return xyz(mapped(t).translation)
names=[str(n) for n in u.AnimPoseExtensions.get_bone_names(rest)]
source_names=set(str(n) for n in u.AnimPoseExtensions.get_bone_names(sr))
arm_names={'l':('upperarm_l','lowerarm_l','hand_l'),'r':('upperarm_r','lowerarm_r','hand_r')}
source_arms={s:tuple('B_'+s.upper()+'_'+n for n in ('arm','elbow','wrist')) for s in ('l','r')}
rest_arms={s:[bone(rest,n) for n in arm_names[s]] for s in ('l','r')}

# The shared skeleton takes upperarm_l translation from the reference pose
# during playback. Encode the desired shoulder position on the hidden clavicle
# so that the upper arm can keep that fixed local translation. Its world
# rotation and scale are still set independently by the arm solve below.
fo.should_retarget = True
left_shoulder_local = xyz(bone(evaluate(base), 'upperarm_l', local).translation)
fo.should_retarget = False

def place_left_shoulder(pose, shoulder):
 clavicle = bone(pose, 'clavicle_l')
 from_clavicle = sub(shoulder, xyz(clavicle.translation))
 bind_direction = rotate(quat(clavicle.rotation), left_shoulder_local)
 scale = length(from_clavicle) / length(left_shoulder_local)
 if scale < .001:
  raise RuntimeError('Source shoulder collapsed onto the clavicle')
 rotation = qmul(from_to(bind_direction, from_clavicle), quat(clavicle.rotation))
 clavicle_track = tf(xyz(clavicle.translation), rotation, (scale, scale, scale))
 return u.AnimPoseExtensions.set_bone_pose(pose, clavicle_track, 'clavicle_l', world)

# Finger proportions come from our rig; articulation comes from the source.
fingers={}
for s in ('l','r'):
 pairs=[]
 for target,source in [('thumb','thumb'),('index','point'),('middle','middle'),('ring','ring'),('pinky','pink')]:
  for i in (1,2,3):
   tn=f'{target}_{i:02d}_{s}';sn=f'B_{s.upper()}_{source}{i}'
   if tn in names and sn in source_names:pairs.append((tn,sn))
 fingers[s]=pairs
# Each hand has its own anatomical wrist-to-palm frame. A right-wrist
# translation cannot be reused for the support hand on a different skin rig.
geometry=runpy.run_path(str(root/'scripts/palm_geometry.py'))
source_hand_bind=u.ERCharacter.get_weapon_reference_bone(source_mesh,'B_L_wrist')
points=[xyz(v) for v in u.ERCharacter.get_presentation_vertices(source_mesh,'B_L_wrist')]
source_palm_bind=u.ERCharacter.get_weapon_reference_bone(source_mesh,'B_L_palm')
points += [xyz(u.MathLibrary.inverse_transform_location(source_hand_bind,u.MathLibrary.transform_location(source_palm_bind,v))) for v in u.ERCharacter.get_presentation_vertices(source_mesh,'B_L_palm')]
source_fingers={target:xyz(u.MathLibrary.inverse_transform_location(source_hand_bind,u.ERCharacter.get_weapon_reference_bone(source_mesh,'B_L_'+source+'1').translation)) for target,source in [('thumb','thumb'),('index','point'),('middle','middle'),('ring','ring'),('pinky','pink')]}
palm_fit=geometry['palm_surface'](points,source_fingers)
source_palm_local=u.Transform(location=u.Vector(*palm_fit['position']),rotation=u.MathLibrary.make_rot_from_xz(u.Vector(*palm_fit['x']),u.Vector(*palm_fit['z'])))
left_socket_relative=u.MathLibrary.make_relative_transform(socket(rest,'ER_Palm_L'),bone(rest,'hand_l'))
# During the source's carry phase the palm and detached magazine travel
# together. Sample the held grip to find its actual surface on the magazine.
# Store that point on the source wrist so the original approach and release
# remain continuous. Evaluate it relative to the real animated magazine.
held_frames = range(15, 35) if reload_only else range(0)
source_mag_name = 'P_mag'
if reload_only:
 sample_time = held_frames[len(held_frames) // 2] / 30
 sample = evaluate(source_reload, sample_time, so)
 source_mag_vertices = u.ERCharacter.get_presentation_vertices(source_mesh, source_mag_name)
 gun_mag_vertices = u.ERCharacter.get_presentation_vertices(data.mesh, source_mag_name)
 if not source_mag_vertices or not gun_mag_vertices:
  raise RuntimeError('Missing original or in-game magazine geometry')
 sample_mag = bone(sample, source_mag_name)
 source_grip_marker = u.MathLibrary.inverse_transform_location(sample_mag, bone(sample, 'B_L_palm').translation)
 source_surface = min(source_mag_vertices, key=lambda point: (point - source_grip_marker).length())
 # Use the matching vertex on the owned runtime magazine. The two imports use
 # the same local geometry, including the conversion embedded in the bone scale.
 gun_surface = min(gun_mag_vertices, key=lambda point: (point - source_surface).length())
 if (gun_surface - source_surface).length() > .001:
  raise RuntimeError('Original and in-game magazine surfaces do not match')
 source_contact_local = u.MathLibrary.inverse_transform_location(
  bone(sample, 'B_L_wrist'), u.MathLibrary.transform_location(sample_mag, source_surface))

def source_left_hand(sp, gun_mag=None):
 wrist = bone(sp, 'B_L_wrist')
 palm = mapped(compose(source_palm_local, wrist))
 if gun_mag is not None:
  # Preserve the authored palm approach/withdrawal in the moving mag frame.
  source_contact = u.MathLibrary.transform_location(wrist, source_contact_local)
  source_local = u.MathLibrary.inverse_transform_location(bone(sp, source_mag_name), source_contact)
  palm.set_editor_property('translation', u.MathLibrary.transform_location(gun_mag, gun_surface + source_local - source_surface))
 palm.set_editor_property('scale3d', rest_arms['l'][2].scale3d)
 return compose(inverse(left_socket_relative), palm), palm

left_reference, _ = source_left_hand(sr)
poses=[];expected=[];expected_palms=[];max_palm=0.;max_contact=0.;max_gun=0.;reach_adjustments=0
max_held_surface_gap=0.
motion = source_reload if reload_only else source_full
gun_motion = data.weapon_reload_animation if reload_only else data.weapon_fire_animation
count = 1 if idle_only else round((motion if reload_only else gun_motion).get_play_length() * 30)
for i in range(count+1):
 time = i / 30
 sp = sr if idle_only else evaluate(motion, time, so)
 gp = gun_rest if idle_only else evaluate(gun_motion, time, go)
 desired_base=mapped(bone(sp,'P_base'))
 desired_gun=compose(inverse(bone(gp,'P_base')),desired_base)
 # Undo the FPS socket attachment, including its logical parent scale. The
 # rendered gun uses absolute unit scale; its attachment offsets do not.
 logical_gun=tf(xyz(desired_gun.translation),quat(desired_gun.rotation),mul(xyz(frame.scale3d),hand.scale3d.x))
 desired_palm=compose(inverse(frame),logical_gun)
 right=compose(inverse(socket_relative),desired_palm)
 source_right=pos(bone(sp,source_arms['r'][2]))
 offset=sub(xyz(right.translation),source_right)
 gun_mag = compose(bone(gp, source_mag_name), desired_gun) if reload_only else None
 left,left_palm=source_left_hand(sp, gun_mag)
 p=evaluate(base)
 desired_hands={}
 for s in ('r','l'):
  sb=[bone(sp,n) for n in source_arms[s]];rb=rest_arms[s]
  branch_offset=offset if s=='r' else sub(xyz(left.translation),pos(sb[2]))
  shoulder,source_elbow,source_wrist=[add(pos(b),branch_offset) for b in sb]
  wrist=xyz(right.translation) if s=='r' else xyz(left.translation)
  upper=length(sub(xyz(rb[1].translation),xyz(rb[0].translation)));lower=length(sub(xyz(rb[2].translation),xyz(rb[1].translation)))
  direction=unit(sub(wrist,shoulder));distance=length(sub(wrist,shoulder));reach=max(abs(upper-lower)+.001,min(upper+lower-.001,distance))
  if abs(reach-distance)>.001:shoulder=add(shoulder,mul(direction,distance-reach));reach_adjustments+=1
  pole=sub(source_elbow,shoulder);bend=sub(pole,mul(direction,dot(pole,direction)))
  if length(bend)<1e-6:bend=(0.,0.,-1.)
  along=(upper*upper-lower*lower+reach*reach)/(2*reach)
  elbow=add(add(shoulder,mul(direction,along)),mul(unit(bend),math.sqrt(max(0.,upper*upper-along*along))))
  rotations=[]
  for j,end in ((0,elbow),(1,wrist)):
   source_delta=qmul(quat(mapped(sb[j]).rotation),inv(quat(mapped(bone(sr,source_arms[s][j])).rotation)))
   original_direction=sub(xyz(rb[j+1].translation),xyz(rb[j].translation))
   rotations.append(qmul(from_to(rotate(source_delta,original_direction),sub(end,shoulder if j==0 else elbow)),qmul(source_delta,quat(rb[j].rotation))))
  hand_rotation=quat(right.rotation) if s=='r' else quat(left.rotation)
  rotations.append(hand_rotation)
  if s == 'l':
   p = place_left_shoulder(p, shoulder)

  for n,position,rotation,b in zip(arm_names[s],(shoulder,elbow,wrist),rotations,rb):p=u.AnimPoseExtensions.set_bone_pose(p,tf(position,rotation,xyz(b.scale3d)),n,world)
  if s == 'l' and length(sub(xyz(bone(p, 'upperarm_l', local).translation), left_shoulder_local)) > .001:
   raise RuntimeError('Left shoulder is not compatible with the runtime translation mode')
  desired_hands[s]=wrist
  # Apply finger articulation relative to the moving wrist.
  sh0=quat(bone(sr,source_arms[s][2]).rotation);sh=quat(sb[2].rotation)
  target_h0=quat(rb[2].rotation)
  anatomical_h0=target_h0 if s=='r' else quat(left_reference.rotation)
  basis=qmul(inv(anatomical_h0),qmul(mapping_q,sh0))
  for tn,sn in fingers[s]:
   rel0=qmul(inv(sh0),quat(bone(sr,sn).rotation));rel=qmul(inv(sh),quat(bone(sp,sn).rotation))
   delta=qmul(qmul(basis,qmul(rel,inv(rel0))),inv(basis))
   tr=qmul(inv(target_h0),quat(bone(rest,tn).rotation))
   current=bone(p,tn);current.set_editor_property('rotation',u.Quat(*qmul(hand_rotation,qmul(delta,tr))))
   p=u.AnimPoseExtensions.set_bone_pose(p,current,tn,world)
 for s in ('l','r'):max_contact=max(max_contact,length(sub(xyz(bone(p,arm_names[s][2]).translation),desired_hands[s])))
 actual_gun=compose(frame,socket(p,'ER_Palm_R'));actual_gun.set_editor_property('scale3d',frame.scale3d)
 max_gun=max(max_gun,(actual_gun.translation-desired_gun.translation).length())
 if abs(dot(quat(actual_gun.rotation),quat(desired_gun.rotation)))<.99999:raise RuntimeError('Weapon root rotation lost source sync')
 max_palm=max(max_palm,(socket(p,'ER_Palm_L').translation-left_palm.translation).length())
 poses.append(p);expected.append(desired_hands);expected_palms.append(xyz(left_palm.translation))
if max_contact>.02 or max_gun>.02 or max_palm>.02:raise RuntimeError('Source contact lost '+str((max_contact,max_gun)))
output=u.load_asset(path) or assets.duplicate_asset(base.get_path_name(),path)
c=output.get_editor_property('controller');c.open_bracket('Fit original pistol pose to FPS rig',False)
try:
 c.set_frame_rate(u.FrameRate(numerator=30,denominator=1),False);c.set_number_of_frames(u.FrameNumber(value=count),False)
 for n in names:
  keys=[bone(p,n,local) for p in poses]
  if not c.set_bone_track_keys(n,[t.translation for t in keys],[t.rotation for t in keys],[t.scale3d for t in keys],False):raise RuntimeError('Cannot save track '+n)
finally:c.close_bracket(False)
save(output)
fo.evaluation_type=u.AnimDataEvalType.COMPRESSED
fo.should_retarget=True
go.should_retarget=True
for i,wanted in enumerate(expected):
 p=evaluate(output,i/30)
 if length(sub(xyz(socket(p,'ER_Palm_L').translation),expected_palms[i]))>.05:raise RuntimeError('Compressed support palm lost source contact')
 if i in held_frames:
  attachment = compose(frame, socket(p, 'ER_Palm_R'))
  attachment.set_editor_property('scale3d', frame.scale3d)
  gun_pose = evaluate(data.weapon_reload_animation, i/30, go)
  mag = compose(bone(gun_pose, source_mag_name), attachment)
  actual_palm = socket(p, 'ER_Palm_L').translation
  gap = (u.MathLibrary.transform_location(mag, gun_surface) - actual_palm).length()
  max_held_surface_gap = max(max_held_surface_gap, gap)
 for s in ('l','r'):
  if length(sub(xyz(bone(p,arm_names[s][2]).translation),wanted[s]))>.05:raise RuntimeError('Compressed wrist lost source motion')
if max_held_surface_gap > 1.2:
 raise RuntimeError(f'Palm slides off the magazine during the source carry: {max_held_surface_gap:.2f} cm')
if fire_only:
 idle_pose = evaluate(data.idle_animation)
 for endpoint in (0., output.get_play_length()):
  firing_pose = evaluate(output, endpoint)
  for chain in arm_names.values():
   for name in chain[1:]:
    if (bone(idle_pose, name).translation - bone(firing_pose, name).translation).length() > .5:
     raise RuntimeError('Fire boundary jumps from the source idle: ' + name)
data.set_editor_property(animation_field, output)
if not reload_only:
 for field, clip in reload_references.items():
  if data.get_editor_property(field) != clip:raise RuntimeError('Reload reference changed: ' + field)
 for file_path, digest in original_reload_hashes.items():
  if sha(file_path) != digest:raise RuntimeError('Reload package changed: ' + str(file_path))
 assets.set_metadata_tag(data, 'ERPistolSource' + mode, '1')
else:
 for tag in ('ERPistolUserReloadStart','ERPistolReloadArmRevision'):assets.remove_metadata_tag(data,tag)
 assets.set_metadata_tag(data,'ERPistolSourceReload','1')
save(data)
for p,digest in protected.items():
 if sha(p)!=digest:raise RuntimeError('Protected package changed '+str(p))
report=dict(animation=output.get_path_name(), source=motion.get_path_name(),
            frames=count+1, source_fps=30, source_elbow_poles=True, independent_support_palm=True,
            max_support_palm_error_cm=max_palm, finger_tracks={s:len(pairs) for s,pairs in fingers.items()},
            max_wrist_error_cm=max_contact, max_weapon_root_error_cm=max_gun,
            anatomical_reach_adjustments=reach_adjustments, protected_packages=len(protected))
if idle_only:
 pose = evaluate(output)
 report['reload_references'] = {field: clip.get_path_name() for field, clip in reload_references.items()}
 report['wrist_distance_from_reload_endpoints_cm'] = {
     side: [length(sub(xyz(bone(pose, names[2]).translation),
                       xyz(bone(evaluate(data.reload_animation, t), names[2]).translation)))
            for t in (0., data.reload_animation.get_play_length())]
     for side, names in arm_names.items()}
elif reload_only:
 report.update(reload=output.get_path_name(), magazine_contact_local=xyz(gun_surface), max_held_magazine_contact_error_cm=max_held_surface_gap)
(backup/'receipt.json').write_text(json.dumps(report,indent=2)+'\n')
u.log('ER_PISTOL_SOURCE_' + mode.upper() + '_READY ' + json.dumps(report))
