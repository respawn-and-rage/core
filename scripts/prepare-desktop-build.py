"""Prepare a Windows-hosted build workspace with shared Content and source plugins.

Code/configuration/branding/plugins are copied; Content is a directory junction
on Windows (symlink elsewhere for tests). Do not edit assets during the build.
The installed engine and original plugin descriptors are never modified.
"""
import argparse
import importlib.util
import json
import os
from pathlib import Path
import shutil
import subprocess


def prepare(project, engine, destination):
    project, engine, destination = project.resolve(), engine.resolve(), destination.resolve()
    if destination.is_relative_to(project) or project.is_relative_to(destination):
        raise ValueError('Build workspace must be outside the source checkout')
    if destination.exists():
        raise ValueError('Build workspace must not already exist')
    spec = importlib.util.spec_from_file_location('archive_plugins', Path(__file__).with_name('archive-linux-project.py'))
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    version_spec = importlib.util.spec_from_file_location('stamp_version', Path(__file__).with_name('stamp-build-version.py'))
    version = importlib.util.module_from_spec(version_spec)
    version_spec.loader.exec_module(version)
    version.ensure_clean(project)
    commit_date = version.source_date(project).isoformat()
    marketplace = module.marketplace_plugins(project, engine)
    commit = subprocess.check_output(['git', '-C', str(project), 'rev-parse', 'HEAD'], text=True).strip()
    ignore = shutil.ignore_patterns('.git', '__pycache__', '.DS_Store', 'Binaries', 'Intermediate',
                                   'GameVersion.txt', 'SourceCommit.txt', 'SourceCommitDate.txt')
    destination.mkdir(parents=True)
    shutil.copy2(project / 'RespawnAndRage.uproject', destination)
    for folder in ('Source', 'Config', 'Build', 'Plugins'):
        if (project / folder).is_dir():
            shutil.copytree(project / folder, destination / folder, ignore=ignore)
    branding = destination / 'Branding'
    branding.mkdir()
    for name in ('background.jpg', 'icon.png', 'Credits.txt'):
        shutil.copy2(project / 'Branding' / name, branding / name)
    shutil.copytree(project / 'Branding/Fonts', branding / 'Fonts', ignore=ignore)
    (destination / 'scripts').mkdir()
    for name in ('stamp-build-version.py', 'export-match-stats.py'):
        shutil.copy2(project / 'scripts' / name, destination / 'scripts')
    (destination / 'Build').mkdir(exist_ok=True)
    (destination / 'Build/SourceCommit.txt').write_text(commit + '\n')
    (destination / 'Build/SourceCommitDate.txt').write_text(commit_date + '\n')
    for descriptor in marketplace:
        target = destination / 'Plugins' / descriptor.stem
        shutil.copytree(descriptor.parent, target, ignore=ignore)
    guest_spec = importlib.util.spec_from_file_location('eos_guest', Path(__file__).with_name('prepare-eos-guest.py'))
    guest_module = importlib.util.module_from_spec(guest_spec)
    guest_spec.loader.exec_module(guest_module)
    guest_module.prepare(destination, engine)
    # Project-local and Marketplace plugins both compile for each target.
    for descriptor in (destination / 'Plugins').rglob('*.uplugin'):
        data = json.loads(descriptor.read_text(encoding='utf-8-sig'))
        if not (descriptor.parent / 'Source').is_dir() and data.get('Modules'):
            raise ValueError('Source required for desktop builds: ' + descriptor.stem)
        data['Installed'] = False
        descriptor.write_text(json.dumps(data, indent=2) + '\n', encoding='utf-8')
    if not (project / 'Content').is_dir():
        raise ValueError('Prepare private assets before creating the workspace')
    if os.name == 'nt':
        # Static PowerShell code and environment values keep path metacharacters
        # out of cmd.exe parsing; Junction does not require symlink privileges.
        subprocess.run(['powershell.exe', '-NoProfile', '-NonInteractive', '-Command',
                        "$ErrorActionPreference='Stop'; New-Item -ItemType Junction "
                        '-Path $env:RR_BUILD_CONTENT_LINK -Target $env:RR_BUILD_CONTENT_SOURCE | Out-Null'],
                       env={**os.environ, 'RR_BUILD_CONTENT_LINK': str(destination / 'Content'),
                            'RR_BUILD_CONTENT_SOURCE': str(project / 'Content')}, check=True)
    else:
        (destination / 'Content').symlink_to(project / 'Content', target_is_directory=True)
    return destination / 'RespawnAndRage.uproject'


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--project', type=Path, default=Path(__file__).resolve().parents[1])
    parser.add_argument('--engine', type=Path, required=True)
    parser.add_argument('--destination', type=Path, required=True)
    args = parser.parse_args()
    print(prepare(args.project, args.engine, args.destination))
