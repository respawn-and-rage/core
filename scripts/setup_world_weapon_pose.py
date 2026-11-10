"""Own world-only grip profiles; preserve imported models and all FPS/gameplay fields."""
from pathlib import Path
import hashlib,json,math,shutil
import unreal as ue
root=Path(ue.Paths.project_dir()).resolve();a=ue.EditorAssetLibrary
backup=root/'Saved/WorldWeaponPoseBackup/2026-10-26/Revision9';backup.mkdir(parents=True,exist_ok=True)
profiles={'Pistol':(.92,-5.),'SMG':(.92,14.),'Shotgun':(.90,20.),'Assault':(1.,18.),'Carbine':(.95,16.),'Sniper':(.78,18.),'Launcher':(.95,14.)}
receipt=[]
GRIP_HEIGHT=5.;GRIP_AXIS=(.75,.65,.1)
for key,(scale,left_x) in profiles.items():
 path=f'/Game/EclipseRecoil/Weapons/Sketchfab/{key}/DA_{key}'
 data=a.load_asset(path)
 if not data:raise RuntimeError('Missing '+path)
 file=root/('Assets/weapons/Content/'+path.removeprefix('/Game/')+'.uasset');old=backup/file.name
 if not old.exists():
  shutil.copy2(file,old)
  if old.read_bytes()!=file.read_bytes():raise RuntimeError('Backup mismatch')
 protected=['grip_transform','first_person_grip_transform','idle_animation','fire_animation','reload_animation','muzzle_offset','damage','armor_ratio','shot_interval','scope_mode','pellet_spread_degrees']
 before={p:str(data.get_editor_property(p)) for p in protected}
 mesh=data.get_editor_property('mesh');idle=data.get_editor_property('weapon_idle_animation')
 vertices=ue.ERCharacter.get_presentation_vertices(mesh,'None')
 rear=min(v.x for v in vertices)
 points=[v for v in vertices if v.x<rear+1.5]
 stock=ue.Vector(rear,sum(v.y for v in points)/len(points),sum(v.z for v in points)/len(points))
 # Right palm contacts the real grip flank, rather than the weapon origin.
 candidates=[]
 for x in ([-5.] if key=='Pistol' else [-4.,-2.,0.,2.]):
  for z in ([2.] if key=='Pistol' else [-4.,-2.,0.,2.]):
   p=ue.ERCharacter.get_weapon_surface_contact(mesh,ue.Vector(x,100,z),ue.Vector(x,-100,z),idle)
   if max(abs(p.x),abs(p.y),abs(p.z))<1000 and abs(p.y)<8:candidates.append(p)
 if not candidates:raise RuntimeError('No right grip contact '+key)
 contact=min(candidates,key=lambda p:(p.x+2)**2+(p.z+2)**2)
 contact=contact+ue.Vector(0,.2,0)
 left=ue.Vector(-4,-3,-3)
 if key!='Pistol':
  wanted=data.get_editor_property('support_hand_offset')
  left_candidates=[]
  for x in range(int(left_x)-4,int(left_x)+21,2):
   # Seat the support palm on the underside. Side contacts combined with an
   # upward palm made the open fingers lie along or through the receiver.
   p=ue.ERCharacter.get_weapon_surface_contact(mesh,ue.Vector(x,0,-100),ue.Vector(x,0,100),idle)
   # Reject a magazine extending below the guard, even if a vertical ray
   # reaches it first. The hand must support the guard itself.
   if max(abs(p.x),abs(p.y),abs(p.z))<1000 and p.z>=wanted.z-8:left_candidates.append(p)
  if not left_candidates:raise RuntimeError('No world support contact '+key)
  left=min(left_candidates,key=lambda p:(p.x-left_x)**2)
  # Revision 7: 6 world cm forward from revision 5 (2 beyond revision 6).
  # Re-sample the guard before applying the explicit thumb seating offset.
  forward_x=left.x+6./scale
  forward_candidates=[]
  for delta in (0.,-.5,.5,-1.,1.):
   x=forward_x+delta/scale
   p=ue.ERCharacter.get_weapon_surface_contact(mesh,ue.Vector(x,0,-100),ue.Vector(x,0,100),idle)
   if max(abs(p.x),abs(p.y),abs(p.z))<1000 and p.z>=wanted.z-8:forward_candidates.append(p)
  if not forward_candidates:raise RuntimeError('No forward support surface '+key)
  left=min(forward_candidates,key=lambda p:abs(p.x-forward_x))
  # Seat the support palm slightly toward the body rather than letting the
  # hand protrude beyond the outside of the guard. Keep a real surface contact
  # at the same longitudinal station, with a smaller inset on narrow guards.
  for inset_cm in (1.5,1.,.5):
   y=left.y-inset_cm/scale
   inner=ue.ERCharacter.get_weapon_surface_contact(mesh,ue.Vector(left.x,y,-100),ue.Vector(left.x,y,100),idle)
   if max(abs(inner.x),abs(inner.y),abs(inner.z))<1000 and abs(inner.z-left.z)<2.5:
    left=inner
    break
  # Requested fine seating: another 2 world cm toward the character's left
  # (weapon -Y), so the thumb lies along the guard. This is the desired palm
  # socket location; the measured guard surface remains the reference above.
  left=left+ue.Vector(0,-2./scale,0)
 left_rotation=ue.MathLibrary.make_rot_from_xz(ue.Vector(math.sin(math.radians(30)),math.cos(math.radians(30)),0),ue.Vector(0,0,1))
 if key=='Launcher':
  # Revision 9: the launcher has a vertical foregrip. Cupping the drum left
  # that grip hanging beside an open hand. Seat the palm on the grip's left
  # flank, facing it, with the fingers leading forward around the front.
  ahead=[v for v in vertices if v.x>left.x+8]
  low=min(v.z for v in ahead)
  grip=[v for v in ahead if v.z<low+3]
  grip_x=sum(v.x for v in grip)/len(grip);grip_z=low+GRIP_HEIGHT/scale
  flank=ue.ERCharacter.get_weapon_surface_contact(mesh,ue.Vector(grip_x,-100,grip_z),ue.Vector(grip_x,100,grip_z),idle)
  if max(abs(flank.x),abs(flank.y),abs(flank.z))>=1000 or abs(flank.y)>6:raise RuntimeError('No launcher foregrip flank '+str((grip_x,grip_z,flank)))
  left=flank;left_rotation=ue.MathLibrary.make_rot_from_xz(ue.Vector(*GRIP_AXIS),ue.Vector(0,1,0))
 values=dict(world_reference_pose=True,world_weapon_scale=scale,world_stock_contact=stock,world_right_palm_contact=contact,
             # Wrist-to-knuckles points forward with a small grip rake. A
             # downward longitudinal axis made the whole hand hang from the gun.
             world_right_palm_rotation=ue.MathLibrary.make_rot_from_xz(ue.Vector(0,0,-1) if key=='Pistol' else ue.Vector(1,0,-.25),ue.Vector(0,-1,0)),
             # Revision 8: weapon yaw -30 degrees from the old +Y direction
             # appears counterclockwise in the user's overhead screenshot.
             # Keep the palm upward and the same measured/seated anchor.
             world_left_palm_contact=left,world_left_palm_rotation=left_rotation)
 for p,v in values.items():data.set_editor_property(p,v)
 if before!={p:str(data.get_editor_property(p)) for p in protected}:raise RuntimeError('Protected weapon/FPS field changed')
 a.set_metadata_tag(data,'ERWorldReferencePoseRevision','9')
 if not a.save_loaded_asset(data,False):raise RuntimeError('Cannot save '+path)
 receipt.append(dict(key=key,scale=scale,length_cm=(max(v.x for v in vertices)-rear)*scale,stock=[stock.x,stock.y,stock.z],right_palm=[contact.x,contact.y,contact.z],left_palm=[left.x,left.y,left.z],before_sha256=hashlib.sha256(old.read_bytes()).hexdigest(),after_sha256=hashlib.sha256(file.read_bytes()).hexdigest(),protected=before))
ref=root/'Assets/characters/SourceAssets/WeaponPoseReferences/2026-10-26';ref.mkdir(parents=True,exist_ok=True)
images=[]
for stamp in ['09.16.17','09.16.34','09.16.23','09.20.59','09.21.06','09.16.43','09.16.51','09.17.03','09.19.28','09.21.13']:
 source=Path('/Users/agea/Desktop')/('Screenshot 2026-10-26 alle '+stamp+'.png')
 if source.exists():
  target=ref/source.name
  if not target.exists():shutil.copy2(source,target)
  if target.read_bytes()!=source.read_bytes():raise RuntimeError('Reference mismatch')
  images.append(dict(file=source.name,sha256=hashlib.sha256(target.read_bytes()).hexdigest()))
(ref/'references.json').write_text(json.dumps(dict(source='User-supplied pose references; white-background rifle primary, pistol and optical pose secondary',images=images),indent=2)+'\n')
(backup/'receipt.json').write_text(json.dumps(receipt,indent=2)+'\n')
(root/'Assets/weapons/SourceAssets/SketchfabWeapons/world-pose-profiles.json').write_text(json.dumps(receipt,indent=2)+'\n')
print('ER_WORLD_POSE_SETUP PASS')
