#!/usr/bin/env python3
"""Stage the owner-supplied warehouse archive and specification privately."""
import argparse
import hashlib
import json
import shutil
import zipfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1] / 'Assets/maps/SourceAssets/FireTraining'
ARCHIVE = 'warehouse-fbx-model-free.zip'
SPEC = 'RR_Warehouse_Killhouse_OpenSpace_Istruzioni.md'
EXPECTED = {'source/Warehouse.fbx', 'textures/WetConcrete_Base_color.png', 'textures/WetConcrete_Roughness@channels=B.png',
            'textures/WetConcrete_Roughness@channels=G.png', 'textures/WetConcrete_Normal_DirectX.png', 'textures/Warehouse_normals.png'}

def sha(path): return hashlib.sha256(path.read_bytes()).hexdigest()

def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--downloads', type=Path, default=Path.home() / 'Downloads')
    args = p.parse_args()
    ROOT.mkdir(parents=True, exist_ok=True)
    archive = ROOT / 'original.zip'
    if not archive.exists(): shutil.copy2(args.downloads / ARCHIVE, archive)
    spec = ROOT / SPEC
    if not spec.exists() and (args.downloads / SPEC).exists(): shutil.copy2(args.downloads / SPEC, spec)
    with zipfile.ZipFile(archive) as package:
        names = {m.filename for m in package.infolist() if not m.is_dir()}
        if names != EXPECTED: raise ValueError(f'Unexpected archive contents: {sorted(names ^ EXPECTED)}')
        if sum(m.file_size for m in package.infolist()) > 64 * 1024**2: raise ValueError('Oversized source')
        for member in package.infolist():
            path = Path(member.filename)
            if path.is_absolute() or '..' in path.parts or (member.external_attr >> 16) & 0o170000 == 0o120000:
                raise ValueError('Unsafe archive member')
            target = ROOT / 'original' / path
            if not target.exists(): package.extract(member, ROOT / 'original')
    entry = dict(name='Warehouse (FBX, free)', archive=ARCHIVE, archive_sha256=sha(archive),
                 author='Unknown; supplied by the project owner on 23 October 2026',
                 license='Not stated inside the archive. Private source; confirm the licence before public distribution.',
                 authoring_tool='Blender 2.91 FBX exporter (from the FBX header)',
                 files={str(f.relative_to(ROOT)): sha(f) for f in sorted((ROOT / 'original').rglob('*')) if f.is_file()},
                 specification=SPEC if spec.exists() else None)
    (ROOT / 'provenance.json').write_text(json.dumps(entry, ensure_ascii=False, indent=2) + '\n')
    (ROOT / 'CREDITS.txt').write_text('Warehouse (FBX, free) — owner-supplied archive ' + ARCHIVE + '\n'
                                      'Author and licence not recorded in the archive; see provenance.json.\n'
                                      'Changes: split into a modular kit, normalized units/pivots, width inserts and '
                                      'plain end-wall bands added; textures reused unchanged.\n')
    print('ER_FIRE_TRAINING_STAGED', entry['archive_sha256'])

if __name__ == '__main__': main()
