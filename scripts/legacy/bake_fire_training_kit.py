"""Blender: split the owner-supplied Warehouse.fbx into the Fire Training kit.

Run: Blender -b --factory-startup --python scripts/bake_fire_training_kit.py
Source: Assets/maps/SourceAssets/FireTraining/original/source/Warehouse.fbx
(SHA-256 recorded in provenance.json). Output: prepared_kit/*.fbx, metres,
unit-scale objects at the origin, plus manifest.json and unclassified report.

The source is one mesh of 439 disconnected parts. Parts are classified by
their measured signature (size, vertex count) and the representative bay
between ribs 20.9 m and 25.1 m. The pitch is 4.2 m between rib centres; the
first source interval is irregular and is not used. Wider kits split the
cross-section at the apex (roof) or beside the portal (end wall) and bridge
the cut with a straight insert; nothing is scaled.
"""
import hashlib
import json
from collections import Counter
from pathlib import Path
import bmesh
import bpy
from mathutils import Vector

root = Path(__file__).resolve().parents[1] / 'Assets/maps/SourceAssets/FireTraining'
source = root / 'original/source/Warehouse.fbx'
out = root / 'prepared_kit'
out.mkdir(exist_ok=True)
SUPPORTED_SHA256 = 'f544a5936677428bb273159225d88b931c05547e38610f6eab8affa60b752bbf'
if hashlib.sha256(source.read_bytes()).hexdigest() != SUPPORTED_SHA256:
    raise RuntimeError('Unsupported Warehouse.fbx revision')
WIDTH_INSERTS = {1600: 0.0, 2000: 4.0, 2400: 8.0}
BAY_CENTER, RIB_Y, END_Y, EAVE, HALF_WIDTH, THICKNESS = 23.0, 25.1, 0.04, 3.0, 8.0, 0.01

bpy.ops.object.select_all(action='SELECT')
bpy.ops.object.delete(use_global=False)
bpy.ops.import_scene.fbx(filepath=str(source))
meshes = [o for o in bpy.context.scene.objects if o.type == 'MESH']
if len(meshes) != 1: raise RuntimeError('Expected one warehouse mesh')
src_obj = meshes[0]
materials = list(src_obj.data.materials)
world = bmesh.new()
world.from_mesh(src_obj.data)
world.transform(src_obj.matrix_world)
world.verts.ensure_lookup_table()
uv_src = world.loops.layers.uv.active

# Connected parts with a measured signature.
parts, seen = [], set()
for v in world.verts:
    if v.index in seen: continue
    stack, verts = [v], []
    seen.add(v.index)
    while stack:
        x = stack.pop(); verts.append(x)
        for e in x.link_edges:
            y = e.other_vert(x)
            if y.index not in seen: seen.add(y.index); stack.append(y)
    faces = {f for x in verts for f in x.link_faces}
    lo = Vector([min(x.co[i] for x in verts) for i in range(3)])
    hi = Vector([max(x.co[i] for x in verts) for i in range(3)])
    parts.append(dict(faces=faces, lo=lo, hi=hi, center=(lo + hi) / 2, size=tuple(round(hi[i] - lo[i], 2) for i in range(3)), verts=len(verts)))
if len(parts) != 439: raise RuntimeError(f'Expected 439 parts, found {len(parts)}')

def pick(size, verts=None, y=None, x=None, ytol=.06):
    found = [p for p in parts if p['size'] == size and (verts is None or p['verts'] == verts)
             and (y is None or abs(p['center'].y - y) < ytol) and (x is None or x(p['center'].x))]
    return found

used = set()
def take(found, expected):
    if len(found) != expected: raise RuntimeError(f'Expected {expected} parts, found {len(found)}')
    for p in found: used.add(id(p))
    return found

left = lambda x: x < 1
SKIN = [(15.98, 4.0, 2.0), (15.97, 4.0, 2.0)]
skins = take([p for s in SKIN for p in pick(s, 38, BAY_CENTER)], 2)
purlins = take(pick((0.08, 3.9, 0.08), 8, BAY_CENTER + .05), 8)
lamps = take(pick((0.15, 3.27, 0.6), 64, BAY_CENTER + .05), 8)
rib = take(pick((16.0, 0.2, 2.09), 76, RIB_Y), 1)
knees = take(pick((0.85, 0.08, 1.67), 8, RIB_Y), 2)
panels = take(pick((0.0, 3.8, 3.0), 4, BAY_CENTER - .1, left), 2)
girts = take(pick((0.1, 3.6, 0.1), 8, BAY_CENTER, left), 4)
lights = take([p for y in (22.15, 23.85) for p in pick((0.1, 1.57, 0.37), 16, y, left)], 2)
post = take(pick((0.2, 0.2, 3.0), 8, RIB_Y - 4.2, left), 1)
gable = take(pick((15.97, 0.0, 5.0), 28, END_Y, ytol=.03), 2)
doors = take(pick((8.09, 0.0, 3.0), 4, None) , 4)
doors = [p for p in doors if p['center'].y < 1]
frame = take(pick((8.09, 0.18, 3.0), 16), 2)
frame = [p for p in frame if p['center'].y < 1]
if len(doors) != 2 or len(frame) != 1: raise RuntimeError('South portal not found')

def new_bm():
    bm = bmesh.new(); bm.loops.layers.uv.new('UVMap'); return bm

def copy_parts(dst, chosen, offset, shift_y=None):
    uv = dst.loops.layers.uv.active
    mapping = {}
    for p in chosen:
        for f in p['faces']:
            verts = []
            for v in f.verts:
                if v not in mapping:
                    co = v.co - offset
                    if shift_y is not None: co.y += shift_y(p)
                    mapping[v] = dst.verts.new(co)
                verts.append(mapping[v])
            nf = dst.faces.new(verts)
            nf.material_index = f.material_index; nf.smooth = f.smooth
            for a, b in zip(f.loops, nf.loops): b[uv].uv = a[uv_src].uv
    dst.normal_update()
    return dst

def affine_uv(face, uv):
    """Least-squares UV = A q + c in the face plane, q in a local 2D basis."""
    n = face.normal
    e1 = (face.loops[1].vert.co - face.loops[0].vert.co).normalized()
    e2 = n.cross(e1).normalized()
    o = face.loops[0].vert.co
    rows = [((l.vert.co - o).dot(e1), (l.vert.co - o).dot(e2), l[uv].uv.copy()) for l in face.loops]
    # Normal equations for [a b c] per UV channel.
    import numpy as np
    M = np.array([[r[0], r[1], 1.0] for r in rows]); result = []
    for ch in range(2):
        t = np.array([r[2][ch] for r in rows])
        sol, *_ = np.linalg.lstsq(M, t, rcond=None); result.append(sol)
    def grad(direction):
        q = (direction.dot(e1), direction.dot(e2))
        return Vector((result[0][0] * q[0] + result[0][1] * q[1], result[1][0] * q[0] + result[1][1] * q[1]))
    return grad

def bridge_insert(bm, plane_x, shift):
    """Split at x=plane_x, move the +x side by shift and bridge the cut faces."""
    if shift == 0: return
    eps = 1e-5
    geom = list(bm.verts) + list(bm.edges) + list(bm.faces)
    bmesh.ops.bisect_plane(bm, geom=geom, plane_co=(plane_x, 0, 0), plane_no=(1, 0, 0), dist=eps)
    bm.verts.ensure_lookup_table(); bm.faces.ensure_lookup_table()
    uv = bm.loops.layers.uv.active
    side = lambda v: 0 if abs(v.co.x - plane_x) <= eps * 10 else (1 if v.co.x > plane_x else -1)
    right = [f for f in bm.faces if any(side(v) > 0 for v in f.verts) and all(side(v) >= 0 for v in f.verts)]
    left_faces = [f for f in bm.faces if any(side(v) < 0 for v in f.verts) and all(side(v) <= 0 for v in f.verts)]
    # Plane edges shared by a left and a right face become bridge quads.
    shared = {}
    for f in left_faces:
        for l in f.loops:
            a, b = l.vert, l.link_loop_next.vert
            if side(a) == 0 and side(b) == 0: shared[frozenset((a, b))] = (f, l)
    records = []
    for f in right:
        loops = [(l.vert, l[uv].uv.copy()) for l in f.loops]
        records.append((loops, f.material_index, f.smooth))
    dup = {}
    for loops, *_ in records:
        for v, _ in loops:
            if side(v) == 0 and v not in dup: dup[v] = bm.verts.new(v.co.copy())
    moving = {v for f in right for v in f.verts if side(v) > 0}
    loose = [v for v in bm.verts if side(v) > 0 and not v.link_faces]
    bmesh.ops.delete(bm, geom=right, context='FACES_ONLY')
    for loops, mat, smooth in records:
        nf = bm.faces.new([dup.get(v, v) for v, _ in loops])
        nf.material_index = mat; nf.smooth = smooth
        for l, (_, coords) in zip(nf.loops, loops): l[uv].uv = coords
        for i, (a, _) in enumerate(loops):
            b = loops[(i + 1) % len(loops)][0]
            key = frozenset((a, b))
            if side(a) == 0 and side(b) == 0 and key in shared:
                lf, ll = shared[key]
                grad = affine_uv(lf, uv)
                step = grad(Vector((shift, 0, 0)))
                uva = next(x[uv].uv for x in lf.loops if x.vert == a)
                uvb = next(x[uv].uv for x in lf.loops if x.vert == b)
                bridge = bm.faces.new([dup[b], dup[a], a, b])
                bridge.material_index = lf.material_index; bridge.smooth = lf.smooth
                for l, coords in zip(bridge.loops, [uvb + step, uva + step, uva, uvb]): l[uv].uv = coords
    for v in moving | set(dup.values()) | set(loose): v.co.x += shift
    # Plane vertices that only belonged to right faces are now unused.
    bmesh.ops.delete(bm, geom=[v for v in bm.verts if not v.link_faces], context='VERTS')
    bm.normal_update()

def move_side(bm, plane_x, shift, positive=True):
    for v in bm.verts:
        if (v.co.x > plane_x) == positive: v.co.x += shift

def split_symmetric(bm, insert, plane=0.0):
    """Bridge at +plane (and -plane when plane>0); keep the result centred."""
    if insert == 0: return
    if plane == 0:
        bridge_insert(bm, 0.0, insert)
        for v in bm.verts: v.co.x -= insert / 2
    else:
        bridge_insert(bm, plane, insert / 2)
        # Mirror the operation on the left: flip, bridge, flip back.
        for v in bm.verts: v.co.x = -v.co.x
        for f in bm.faces: f.normal_flip()
        bridge_insert(bm, plane, insert / 2)
        for v in bm.verts: v.co.x = -v.co.x
        for f in bm.faces: f.normal_flip()
    bm.normal_update()

def duplicate_part(bm, target_x, source_x, lift=0.0):
    """Copy the disconnected part centred nearest source_x to target_x."""
    bm.verts.ensure_lookup_table()
    seen, best = set(), None
    for v in bm.verts:
        if v.index in seen: continue
        stack, comp = [v], []
        seen.add(v.index)
        while stack:
            x = stack.pop(); comp.append(x)
            for e in x.link_edges:
                y = e.other_vert(x)
                if y.index not in seen: seen.add(y.index); stack.append(y)
        cx = (min(x.co.x for x in comp) + max(x.co.x for x in comp)) / 2
        if best is None or abs(cx - source_x) < abs(best[0] - source_x): best = (cx, comp)
    cx, comp = best
    faces = {f for v in comp for f in v.link_faces}
    result = bmesh.ops.duplicate(bm, geom=list(faces))
    for v in [g for g in result['geom'] if isinstance(g, bmesh.types.BMVert)]:
        v.co.x += target_x - cx; v.co.z += lift

def apex_lift(x):
    """Inner skin height at the apex relative to x; inserted fittings follow it."""
    zs = lambda px: min(v.co.z for p in skins for f in p['faces'] for v in f.verts if abs(v.co.x - HALF_WIDTH - px) < .3)
    return zs(0.0) - zs(x)

def plain_band(width, template):
    """Two-sided plain end-wall band; UVs follow the source gable mapping."""
    bm = new_bm(); uv = bm.loops.layers.uv.active
    half = width / 2 - .015
    for sign in (-1, 1):
        faces = [f for f in template.faces if f.normal.y * sign > .5]
        grads = affine_uv(faces[0], template.loops.layers.uv.active)
        ref = faces[0].loops[0]
        y = THICKNESS if sign > 0 else 0.0
        # Counter-clockwise seen from the side the face points to (-Y outside, +Y inside).
        corners = [Vector((-half, y, 0)), Vector((half, y, 0)), Vector((half, y, 3.0)), Vector((-half, y, 3.0))]
        if sign > 0: corners.reverse()
        f = bm.faces.new([bm.verts.new(c) for c in corners])
        f.material_index = faces[0].material_index
        for l in f.loops: l[uv].uv = ref[template.loops.layers.uv.active].uv + grads(l.vert.co - ref.vert.co)
        f.normal_update()
        if (f.normal.y > 0) != (sign > 0): raise RuntimeError('End band face orientation')
    bm.normal_update(); return bm

def thicken(bm):
    """Separate the coincident two-sided sheet by 1 cm so its bounds have depth."""
    inner = {v for f in bm.faces if f.normal.y > .5 for v in f.verts}
    if inner & {v for f in bm.faces if f.normal.y < -.5 for v in f.verts}: raise RuntimeError('Shared sheet vertices')
    for v in inner: v.co.y += THICKNESS
    return bm

def floor_clamp(bm):
    """Source portal/gable bases dip 2.4 cm below the floor; rest them on it."""
    for v in bm.verts: v.co.z = max(v.co.z, 0.0)
    return bm

def export(name, bm, role, width=None):
    mesh = bpy.data.meshes.new(name)
    used_slots = sorted({f.material_index for f in bm.faces})
    remap = {old: new for new, old in enumerate(used_slots)}
    for f in bm.faces: f.material_index = remap[f.material_index]
    bm.to_mesh(mesh)
    for old in used_slots: mesh.materials.append(materials[old])
    mesh.use_auto_smooth = True; mesh.auto_smooth_angle = 0.5236
    obj = bpy.data.objects.new(name, mesh); bpy.context.scene.collection.objects.link(obj)
    bpy.ops.object.select_all(action='DESELECT'); obj.select_set(True); bpy.context.view_layer.objects.active = obj
    target = out / (name + '.fbx')
    bpy.ops.export_scene.fbx(filepath=str(target), use_selection=True, object_types={'MESH'}, mesh_smooth_type='FACE',
                             add_leaf_bones=False, bake_anim=False, axis_forward='-Y', axis_up='Z')
    lo = [min(v.co[i] for v in mesh.vertices) for i in range(3)]
    hi = [max(v.co[i] for v in mesh.vertices) for i in range(3)]
    receipt.append(dict(name=name, role=role, width_cm=width, materials=[m.name for m in mesh.materials],
                        triangles=sum(len(p.vertices) - 2 for p in mesh.polygons),
                        bounds_min_m=[round(x, 4) for x in lo], bounds_max_m=[round(x, 4) for x in hi],
                        sha256=hashlib.sha256(target.read_bytes()).hexdigest()))
    bpy.data.objects.remove(obj); bm.free()

receipt = []
centre = lambda y, z=0.0: Vector((HALF_WIDTH, y, z))
# Width-independent modules. Side walls are the west (x=0) side; the east side
# is the same module turned 180 degrees by the generator's half-turn.
wall = copy_parts(new_bm(), panels, Vector((0, BAY_CENTER, 0)), shift_y=lambda p: .1)  # centre the 3.8 m sheet
copy_parts(wall, girts, Vector((0, BAY_CENTER, 0)))
export('SM_ER_WH_SideWallBay', wall, 'SideWallBay')
export('SM_ER_WH_SideWallLamps', copy_parts(new_bm(), lights, Vector((0, BAY_CENTER, 0))), 'SideWallLamps')
export('SM_ER_WH_Post', copy_parts(new_bm(), post, Vector((0, RIB_Y - 4.2, 0))), 'Post')
export('SM_ER_WH_EndPortal', floor_clamp(copy_parts(new_bm(), doors + frame, Vector((HALF_WIDTH, END_Y, 0)))), 'EndPortal')
for width, insert in WIDTH_INSERTS.items():
    suffix = f'_W{width}'
    roof = copy_parts(new_bm(), skins + purlins, centre(BAY_CENTER, EAVE))
    lift = apex_lift(1.08)
    split_symmetric(roof, insert)
    targets = [0.0] if insert == 4.0 else [-2.0, 2.0] if insert == 8.0 else []
    for x in targets: duplicate_part(roof, x, 1.08 + insert / 2, lift)
    export('SM_ER_WH_RoofBay' + suffix, roof, 'RoofBay', width)
    lamp = copy_parts(new_bm(), lamps, centre(BAY_CENTER, EAVE))
    split_symmetric(lamp, insert)
    for x in targets: duplicate_part(lamp, x, 1.08 + insert / 2, lift)
    export('SM_ER_WH_RoofLamps' + suffix, lamp, 'RoofLamps', width)
    arch = copy_parts(new_bm(), rib + knees, centre(RIB_Y, EAVE))
    split_symmetric(arch, insert)
    export('SM_ER_WH_RoofArch' + suffix, arch, 'RoofArch', width)
    end = floor_clamp(thicken(copy_parts(new_bm(), gable, centre(END_Y))))
    bmesh.ops.bisect_plane(end, geom=list(end.verts) + list(end.edges) + list(end.faces), plane_co=(0, 0, EAVE), plane_no=(0, 0, 1), dist=1e-5)
    crown = end.copy()
    bmesh.ops.delete(end, geom=[f for f in end.faces if f.calc_center_median().z > EAVE], context='FACES')
    bmesh.ops.delete(crown, geom=[f for f in crown.faces if f.calc_center_median().z < EAVE], context='FACES')
    for v in crown.verts: v.co.z -= EAVE
    band = plain_band(16.0 + insert, end)
    split_symmetric(end, insert, 6.0)
    split_symmetric(crown, insert)
    export('SM_ER_WH_EndGround' + suffix, end, 'EndGround', width)
    export('SM_ER_WH_EndBand' + suffix, band, 'EndBand', width)
    export('SM_ER_WH_EndCrown' + suffix, crown, 'EndCrown', width)

unclassified = Counter(p['size'] for p in parts if id(p) not in used)
(out / 'manifest.json').write_text(json.dumps(dict(
    source_sha256=SUPPORTED_SHA256, bay_pitch_cm=420, wall_band_cm=300, roof_rise_cm=200, widths_cm=sorted(WIDTH_INSERTS),
    frame='Blender metres: X across the width (centred), Y along the length (bay/rib centre), Z up (wall band base or eave)',
    modules=receipt), indent=2) + '\n')
(out / 'unclassified.json').write_text(json.dumps(dict(
    note='Representative parts were taken from one bay; the rest are source repetitions of the same families.',
    repeated_parts={str(k): v for k, v in sorted(unclassified.items(), key=lambda kv: -kv[1])}), indent=2) + '\n')
print('ER_FIRE_TRAINING_KIT_BAKED', len(receipt))
