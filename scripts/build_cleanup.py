"""Remove completed Mac build payloads before measuring free disk space."""
import json
from pathlib import Path
import re
import stat
import sys
import shutil
import time


def cleanup_old_builds(roots, *, dry_run=False):
    """Keep receipts/logs, incomplete releases and builds awaiting upload.

    Only inspect immediate children of explicit build roots. Never follow a
    symlink or use a deletion path supplied by a receipt.
    """
    for root in dict.fromkeys(Path(p).resolve() for p in roots):
        if not root.is_dir():
            continue
        for run in sorted(root.iterdir()):
            if run.is_symlink() or not run.is_dir():
                continue
            release = run / 'release.json'
            receipt = release if release.exists() else run / 'receipt.json'
            if receipt.is_symlink() or not receipt.is_file():
                continue
            try:
                data = json.loads(receipt.read_text())
            except (OSError, ValueError):
                continue
            if not isinstance(data, dict):
                continue
            expected = 'uploaded' if receipt == release else 'packaged'
            if data.get('status') != expected:
                continue
            names = ('build/project', 'build/dist', 'scratch', 'diagnostics') if receipt == release else ('project', 'dist')
            targets = [run / name for name in names]
            # Xcode's retained notarization archive is also a build payload.
            archive_root = run / 'build' if receipt == release else run
            if not archive_root.is_symlink():
                targets.extend(archive_root.glob('RespawnAndRage*.xcarchive'))
            targets = [p for p in targets if p.is_dir() and
                       not p.is_symlink() and not p.parent.is_symlink() and
                       p.resolve().is_relative_to(run)]
            if not targets:
                continue
            for target in targets:
                print(('Would remove old build: ' if dry_run else 'Removing old build: ') + str(target), flush=True)
            if dry_run:
                continue
            # Mark before deleting: an interrupted cleanup cannot be resumed as a package.
            data['payload_removed_at'] = time.strftime('%Y-%m-%dT%H:%M:%S%z')
            if receipt != release:
                data['scratch_removed'] = True
            temporary = receipt.with_suffix('.json.tmp')
            temporary.write_text(json.dumps(data, indent=2) + '\n')
            temporary.replace(receipt)
            for target in targets:
                shutil.rmtree(target)


def build_roots(workspace, project):
    return (workspace, project.parent / 'builds/macos', Path('/private/tmp/rr-sm5-releases'))


def remove_uploaded_build(run, project):
    """Remove only the selected successful run, including its logs and receipts."""
    run, project = Path(run), Path(project).resolve()
    if run.is_symlink() or getattr(run.lstat(), 'st_file_attributes', 0) & stat.FILE_ATTRIBUTE_REPARSE_POINT:
        raise ValueError('Refusing a symlinked build directory: ' + str(run))
    resolved = run.resolve()
    if resolved.is_relative_to(project) or project.is_relative_to(resolved):
        raise ValueError('Build cleanup must stay outside the source project')
    # Check the state at the selected path; never take deletion paths from JSON.
    receipt = next((run / name for name in ('release.json', 'summary.json', 'receipt.json')
                    if (run / name).is_file()), None)
    if receipt is None or receipt.is_symlink():
        raise ValueError('Missing build receipt: ' + str(run))
    data = json.loads(receipt.read_text(encoding='utf-8-sig'))
    if not isinstance(data, dict) or data.get('status') != 'uploaded':
        raise ValueError('Only successfully uploaded builds can be removed: ' + str(run))
    print('Removing uploaded build, logs and receipts: ' + str(run), flush=True)
    shutil.rmtree(run)


def cleanup_uploaded_desktop_runs(root, project):
    """Remove old timestamped runs with a successful whole-run summary."""
    root = Path(root)
    if not root.is_dir():
        return
    for run in sorted(root.iterdir()):
        if not re.fullmatch(r'[0-9]{8}-[0-9]{6}-[0-9a-f]{8}', run.name):
            continue
        summary = run / 'summary.json'
        if not summary.is_file() or summary.is_symlink():
            continue
        try:
            data = json.loads(summary.read_text(encoding='utf-8-sig'))
            if isinstance(data, dict) and data.get('status') == 'uploaded':
                remove_uploaded_build(run, project)
        except (OSError, ValueError) as error:
            print(f'Warning: could not clean desktop run {run}: {error}', file=sys.stderr)


if __name__ == '__main__':
    import argparse
    parser = argparse.ArgumentParser(description='Remove a successfully uploaded build and all its artifacts.')
    group = parser.add_mutually_exclusive_group(required=True)
    group.add_argument('--uploaded-run', type=Path)
    group.add_argument('--uploaded-desktop-runs', type=Path)
    args = parser.parse_args()
    project = Path(__file__).resolve().parents[1]
    if args.uploaded_run:
        remove_uploaded_build(args.uploaded_run, project)
    else:
        cleanup_uploaded_desktop_runs(args.uploaded_desktop_runs, project)
