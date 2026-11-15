#!/usr/bin/env python3
"""Mount private submodules without changing Unreal /Game package paths.

--init fetches pinned submodules and LFS objects. --check is read-only.
Never overwrites existing files/directories or mounts unresolved LFS pointers.
Close Unreal before initializing or changing mounts.
"""
import argparse
import json
import os
from pathlib import Path
import stat
import subprocess
import sys

PROJECT = Path(__file__).resolve().parents[1]
LFS_HEADER = b'version https://git-lfs.github.com/spec/v1'


def layout(root):
    data = json.loads((root / 'assets.json').read_text())
    result = []
    for name, repo in data['repositories'].items():
        base = Path(repo['path'])
        if base.is_absolute() or '..' in base.parts or base.parts[0] != 'Assets':
            raise RuntimeError('Invalid submodule path: ' + str(base))
        for entry in repo['mounts']:
            relative = Path(entry)
            if relative.is_absolute() or '..' in relative.parts or relative.parts[0] not in ('Content', 'SourceAssets'):
                raise RuntimeError('Invalid mount: ' + entry)
            result.append((name, root / base / relative, root / relative))
    destinations = [d for _, _, d in result]
    if len(set(destinations)) != len(destinations) or any(a in b.parents for a in destinations for b in destinations if a != b):
        raise RuntimeError('Overlapping asset mounts')
    return result


def is_link(path):
    if path.is_symlink():
        return True
    if hasattr(path, 'is_junction'):
        return path.is_junction()
    # Path.is_junction arrived in 3.12; lstat exposes the Windows reparse
    # tag since 3.8. Do not mistake ordinary directories/other reparse tags
    # for asset junctions, and do not follow a broken junction here.
    try:
        return getattr(path.lstat(), 'st_reparse_tag', None) == getattr(stat, 'IO_REPARSE_TAG_MOUNT_POINT', 0xA0000003)
    except FileNotFoundError:
        return False


def validate(root, require_mounts=False):
    entries = []
    problems = []
    for name, source, destination in layout(root):
        if not source.is_dir():
            optional_source = destination.relative_to(root).parts[0] == 'SourceAssets'
            if optional_source and not source.exists() and not source.is_symlink():
                continue
            problems.append('Missing submodule content: ' + str(source))
            continue
        entries.append((name, source, destination))
        for file in source.rglob('*'):
            if file.is_file():
                with file.open('rb') as stream:
                    if stream.read(len(LFS_HEADER)) == LFS_HEADER:
                        problems.append('Unresolved LFS pointer: ' + str(file))
        if is_link(destination):
            if destination.resolve() != source.resolve():
                problems.append('Conflicting mount: ' + str(destination))
        elif destination.exists():
            problems.append('Refusing to replace existing data: ' + str(destination))
        elif require_mounts:
            problems.append('Missing mount: ' + str(destination))
    if problems:
        raise RuntimeError('\n'.join(problems))
    return entries


def mount(root):
    entries = validate(root)  # Validate the entire plan before any writes.
    for _, source, destination in entries:
        if is_link(destination):
            continue
        destination.parent.mkdir(parents=True, exist_ok=True)
        if os.name == 'nt':
            subprocess.run(['cmd', '/c', 'mklink', '/J', str(destination), str(source)], check=True)
        else:
            destination.symlink_to(os.path.relpath(source, destination.parent), target_is_directory=True)
    validate(root, require_mounts=True)
    return len(entries)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--init', action='store_true')
    parser.add_argument('--check', action='store_true')
    args = parser.parse_args()
    if args.init and args.check:
        parser.error('--init cannot be combined with read-only --check')
    try:
        if args.init:
            subprocess.run(['git', 'submodule', 'sync', '--recursive'], cwd=PROJECT, check=True)
            subprocess.run(['git', 'submodule', 'update', '--init', '--recursive'], cwd=PROJECT, check=True)
            for repo in json.loads((PROJECT / 'assets.json').read_text())['repositories'].values():
                folder = PROJECT / repo['path']
                subprocess.run(['git', 'lfs', 'install', '--local'], cwd=folder, check=True)
                subprocess.run(['git', 'lfs', 'pull'], cwd=folder, check=True)
        count = len(validate(PROJECT, require_mounts=True)) if args.check else mount(PROJECT)
        print(f'ASSET_MOUNTS_PASS: {count} mounts, no unresolved LFS pointers')
    except (RuntimeError, subprocess.CalledProcessError, OSError) as error:
        print(str(error), file=sys.stderr)
        return 1
    return 0


if __name__ == '__main__':
    sys.exit(main())
