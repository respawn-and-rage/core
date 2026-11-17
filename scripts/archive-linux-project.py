"""Create the private Linux build input, including source for Marketplace plugins.

Engine-installed Marketplace plugins are absent from the cached Linux engine.
Include their source as project plugins; never copy Mac binaries/intermediates.
"""
import argparse
from contextlib import contextmanager
import gzip
import hashlib
import io
import importlib.util
import json
from pathlib import Path
import subprocess
import sys
import tarfile


@contextmanager
def project_archive(output, streaming):
    if streaming:
        # Older Python versions cannot pass compresslevel through tar's
        # streaming gzip mode. A separate gzip writer works on Python 3.9+.
        with gzip.GzipFile(fileobj=sys.stdout.buffer, mode='wb', compresslevel=1) as compressed:
            with tarfile.open(fileobj=compressed, mode='w|', dereference=True) as archive:
                yield archive
    else:
        with tarfile.open(output, 'w:gz', dereference=True, compresslevel=1) as archive:
            yield archive


def marketplace_plugins(project, engine):
    descriptor = json.loads((project / 'RespawnAndRage.uproject').read_text())
    candidates = list((engine / 'Engine/Plugins/Marketplace').rglob('*.uplugin'))
    plugins = []
    for reference in descriptor.get('Plugins', []):
        if not reference.get('Enabled'):
            continue
        name = reference['Name']
        if any((project / 'Plugins').rglob(name + '.uplugin')):
            continue
        matches = [p for p in candidates if p.stem == name]
        if len(matches) > 1:
            raise ValueError('Ambiguous Marketplace plugin: ' + name)
        if not matches:
            continue
        plugin = matches[0]
        if not (plugin.parent / 'Source').is_dir():
            raise ValueError('Linux needs plugin source: ' + name)
        plugins.append(plugin)
    return plugins


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('output', type=Path)
    parser.add_argument('--project', type=Path, default=Path(__file__).resolve().parents[1])
    parser.add_argument('--engine', type=Path, default=Path('/Users/Shared/Epic Games/UE_5.8'))
    parser.add_argument('--check', action='store_true')
    args = parser.parse_args()
    project = args.project.resolve()
    streaming = str(args.output) == '-'
    output = None if streaming else args.output.resolve()
    if output and output.is_relative_to(project):
        parser.error('Write private build inputs outside the public game repository')
    plugins = marketplace_plugins(project, args.engine)
    manifest = [{'name': p.stem, 'version': json.loads(p.read_text()).get('VersionName'),
                 'descriptor_sha256': hashlib.sha256(p.read_bytes()).hexdigest()} for p in plugins]
    print(json.dumps({'marketplace_plugins': manifest}), file=sys.stderr if streaming else sys.stdout, flush=True)
    if args.check:
        return
    guest_spec = importlib.util.spec_from_file_location('eos_guest', Path(__file__).with_name('prepare-eos-guest.py'))
    guest_module = importlib.util.module_from_spec(guest_spec)
    guest_spec.loader.exec_module(guest_module)
    guest_module.prepare(project, args.engine)
    if output:
        output.parent.mkdir(parents=True, exist_ok=True)
    commit = subprocess.check_output(['git', '-C', str(project), 'rev-parse', 'HEAD'], text=True).strip()

    def source_only(member):
        parts = Path(member.name).parts
        if any(p in ('.git', '__pycache__', '.DS_Store') for p in parts):
            return None
        if 'Plugins' in parts and any(p in ('Binaries', 'Intermediate') for p in parts):
            return None
        if member.name in ('Build/GameVersion.txt', 'Build/SourceCommit.txt', 'Build/SourceCommitDate.txt'):
            return None
        return member

    with project_archive(output, streaming) as archive:
        def add_bytes(name, value):
            data = value.encode()
            member = tarfile.TarInfo(name)
            member.size, member.mode = len(data), 0o644
            archive.addfile(member, io.BytesIO(data))

        for name in ('RespawnAndRage.uproject', 'Config', 'Content', 'Source', 'Build', 'Plugins'):
            if (project / name).exists():
                archive.add(project / name, arcname=name, filter=source_only)
        for name in ('stamp-build-version.py', 'export-match-stats.py', 'stage-match-stats.py'):
            archive.add(project / 'scripts' / name, arcname='scripts/' + name)
        add_bytes('Build/SourceCommit.txt', commit + '\n')
        commit_date = subprocess.check_output(['git', '-C', str(project), 'show', '-s', '--format=%cI', 'HEAD'], text=True).strip()
        add_bytes('Build/SourceCommitDate.txt', commit_date + '\n')
        add_bytes('Build/ExternalPlugins.json', json.dumps(manifest, indent=2) + '\n')
        for name in ('background.jpg', 'icon.png', 'Credits.txt'):
            archive.add(project / 'Branding' / name, arcname='Branding/' + name)
        archive.add(project / 'Branding/Fonts', arcname='Branding/Fonts', filter=source_only)
        for plugin in plugins:
            prefix = 'Plugins/' + plugin.stem
            # Keep the installed engine untouched. This disposable project copy
            # compiles from source for Linux instead of expecting Mac binaries.
            data = json.loads(plugin.read_text())
            data['Installed'] = False
            add_bytes(prefix + '/' + plugin.name, json.dumps(data, indent=2) + '\n')
            for child in plugin.parent.iterdir():
                if child.name in ('Binaries', 'Intermediate', '.git', plugin.name):
                    continue
                archive.add(child, arcname=prefix + '/' + child.name, filter=source_only)
    print('STREAM_ARCHIVE_COMPLETE' if streaming else output, file=sys.stderr if streaming else sys.stdout, flush=True)


if __name__ == '__main__':
    main()
