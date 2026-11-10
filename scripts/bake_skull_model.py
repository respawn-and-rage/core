"""Extract the supplied coin and export an upright, centered 24 cm mesh.

Run in Blender with --python scripts/bake_skull_model.py -- <archive> <output-dir>.
"""
import math
from pathlib import Path
import sys
from tempfile import TemporaryDirectory
from zipfile import ZipFile

import bpy
from mathutils import Matrix

archive, output = map(Path, sys.argv[sys.argv.index('--') + 1:])
output.mkdir(parents=True, exist_ok=True)
bpy.ops.wm.read_factory_settings(use_empty=True)
with ZipFile(archive) as source, TemporaryDirectory() as staging:
    fbx = source.extract('source/Coins2.fbx', staging)
    bpy.ops.import_scene.fbx(filepath=fbx)
    (output / 'T_Skull.png').write_bytes(source.read('textures/coin.png'))

coin = bpy.data.objects['pCylinder1']
coin.parent = None
coin.matrix_world = Matrix.Identity(4)
diameter = max(v.co.y for v in coin.data.vertices) - min(v.co.y for v in coin.data.vertices)
coin.data.transform(Matrix.Rotation(math.pi / 2, 4, 'X') @ Matrix.Scale(.24 / diameter, 4))
coin.name = 'SM_Skull'
bpy.ops.object.select_all(action='DESELECT')
coin.select_set(True)
bpy.context.view_layer.objects.active = coin
bpy.ops.export_scene.fbx(filepath=str(output / 'SM_Skull.fbx'), use_selection=True,
                         object_types={'MESH'}, bake_anim=False)
