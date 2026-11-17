"""Package an isolated project copy on macOS using the existing UE Installed Build.
Default: universal Intel + Apple Silicon, Development. No publication/signing identity.
"""
import argparse
import json
import os
from pathlib import Path
import platform
import plistlib
import shutil
import signal
import subprocess
import sys
import time
import importlib.util

PROJECT_ROOT = Path(__file__).resolve().parents[1]
p = argparse.ArgumentParser(description=__doc__)
p.add_argument('--project', type=Path, default=PROJECT_ROOT)
p.add_argument('--engine', type=Path, default=Path('/Users/Shared/Epic Games/UE_5.8'))
p.add_argument('--workspace', type=Path, default=PROJECT_ROOT.parent / 'builds' / 'macos')
p.add_argument('--architecture', choices=['universal', 'arm64', 'x64'], default='universal')
p.add_argument('--configuration', choices=['Development', 'Shipping'], default='Development')
p.add_argument('--resume', type=Path, help='Reuse an existing isolated build directory')
p.add_argument('--run-directory', type=Path, help='New attempt directory inside --workspace (used by release-macos.py)')
p.add_argument('--recook-only', action='store_true', help='With --resume, reuse compiled binaries and rerun only cook/stage/package')
p.add_argument('--xcode-stdin-workaround', action='store_true', help='Finalize outside UBA for the UE5.8.3/Xcode27 closed-stdin bug')
p.add_argument('--check', action='store_true', help='Environment/space check with a temporary APFS clone probe')
a = p.parse_args()
if a.run_directory and a.resume:
    p.error('--run-directory cannot be combined with --resume')
if a.recook_only and not a.resume:
    p.error('--recook-only requires --resume')
project, engine, workspace = a.project.resolve(), a.engine.resolve(), a.workspace.resolve()
if a.run_directory:
    a.run_directory = a.run_directory.resolve()
    if not a.run_directory.is_relative_to(workspace) or a.run_directory == workspace or a.run_directory.exists():
        p.error('--run-directory must be a new directory inside --workspace')
if platform.system() != 'Darwin':
    raise RuntimeError('Run on macOS with Xcode and UE 5.8.3 installed')
if workspace.is_relative_to(project) or project.is_relative_to(workspace):
    raise ValueError('Use a workspace outside the original project')
if not (project / 'RespawnAndRage.uproject').is_file():
    raise ValueError('RespawnAndRage.uproject missing')
from build_cleanup import build_roots, cleanup_old_builds
if not a.resume:
    if not a.check:
        idle_spec = importlib.util.spec_from_file_location('clean_project', Path(__file__).with_name('clean-project.py'))
        idle_module = importlib.util.module_from_spec(idle_spec)
        idle_spec.loader.exec_module(idle_module)
        idle_module.ensure_idle()
    cleanup_old_builds(build_roots(workspace, project), dry_run=a.check)
v = json.loads((engine / 'Engine/Build/Build.version').read_text())
if tuple(v[k] for k in ('MajorVersion', 'MinorVersion', 'PatchVersion')) != (5, 8, 3):
    raise ValueError('UE 5.8.3 required')
xcode_version = subprocess.check_output(['xcodebuild', '-version'], text=True)
print(xcode_version, end='')
if platform.mac_ver()[0].split('.')[0] == '27' and xcode_version.startswith('Xcode 27.'):
    a.xcode_stdin_workaround = True
subprocess.run(['xcrun', '--find', 'clang'], check=True)
nearest = workspace
while not nearest.exists():
    nearest = nearest.parent
from apfs_snapshot import clone_supported, copy_file
use_clones = clone_supported(project / 'RespawnAndRage.uproject', nearest)
# The earlier 40 GiB included a complete second content tree. APFS clones
# preserve isolation without immediately allocating another copy of every asset.
minimum_gib = (25 if a.architecture == 'universal' else 20) if use_clones else 40
if a.resume:
    minimum_gib = 8  # Existing snapshot and compiled objects are reused.
minimum = minimum_gib * 1024**3
free = shutil.disk_usage(nearest).free
if not a.check and not a.resume and free < minimum:
    free = idle_module.reclaim_build_space(project, nearest, minimum)
print(json.dumps({'engine': str(engine), 'workspace': str(workspace),
                  'architecture': a.architecture, 'xcode_stdin_workaround': a.xcode_stdin_workaround, 'free_gib': round(free / 1024**3, 1),
                  'minimum_free_gib': minimum_gib, 'apfs_clones': use_clones, 'space_ok': free >= minimum}), flush=True)
if a.check:
    raise SystemExit(0 if free >= minimum else 2)
if free < minimum:
    raise RuntimeError(f'Need at least {minimum_gib} GiB free before starting; found {free / 1024**3:.1f} GiB. Choose another volume with --workspace.')
if not a.resume:
    subprocess.run([sys.executable, str(project / 'scripts/stamp-build-version.py'), '--check-clean'], check=True)
if a.resume:
    run_dir = a.resume.resolve()
    if not run_dir.is_relative_to(workspace) or not (run_dir / 'receipt.json').is_file():
        raise ValueError('Resume only a previous build inside this workspace')
    snapshot = run_dir / 'project'
    previous = json.loads((run_dir / 'receipt.json').read_text())
    if previous.get('scratch_removed'):
        raise ValueError('This successful build was cleaned to recover disk space; start a fresh build instead of resuming')
    if not (snapshot / 'scripts/stamp-build-version.py').is_file():
        raise ValueError('This snapshot predates build version stamping; start a fresh build')
    expected_arches = 'arm64+x64' if a.architecture == 'universal' else a.architecture
    if previous['architecture'] != expected_arches or not (snapshot / 'RespawnAndRage.uproject').is_file():
        raise ValueError('Resume architecture or project mismatch')
else:
    run_dir = a.run_directory or workspace / time.strftime('%Y%m%d-%H%M%S')
    snapshot = run_dir / 'project'
    snapshot.mkdir(parents=True)
    # Follow the private asset mounts while excluding unrelated source archives,
    # repository data, previous binaries and caches. Never build in the live checkout.
    for name in ('RespawnAndRage.uproject', 'Config', 'Content', 'Source', 'Build', 'Plugins'):
        src, dst = project / name, snapshot / name
        if src.is_dir():
            shutil.copytree(src, dst, symlinks=False, copy_function=copy_file if use_clones else shutil.copy2, ignore=shutil.ignore_patterns('.git', '__pycache__', '.DS_Store'))
        elif src.is_file():
            (copy_file if use_clones else shutil.copy2)(src, dst)
    # Installed Marketplace binaries cover the Editor, but do not necessarily
    # include Game/universal manifests. Compile owned build copies from source.
    plugin_spec = importlib.util.spec_from_file_location('build_plugins', Path(__file__).with_name('archive-linux-project.py'))
    plugin_module = importlib.util.module_from_spec(plugin_spec)
    plugin_spec.loader.exec_module(plugin_module)
    for plugin in plugin_module.marketplace_plugins(project, engine):
        destination = snapshot / 'Plugins' / plugin.stem
        shutil.copytree(plugin.parent, destination, symlinks=False,
                        copy_function=copy_file if use_clones else shutil.copy2,
                        ignore=shutil.ignore_patterns('Binaries', 'Intermediate', '.git', '__pycache__', '.DS_Store'))
        descriptor = destination / plugin.name
        data = json.loads(descriptor.read_text())
        data['Installed'] = False
        descriptor.write_text(json.dumps(data, indent=2) + '\n')
    branding = snapshot / 'Branding'
    branding.mkdir(exist_ok=True)
    for name in ('background.jpg', 'icon.png', 'Credits.txt'):
        (copy_file if use_clones else shutil.copy2)(project / 'Branding' / name, branding / name)
    shutil.copytree(project / 'Branding/Fonts', branding / 'Fonts',
                    copy_function=copy_file if use_clones else shutil.copy2)
    # Preserve the checkout identity without copying .git or requiring it at runtime.
    (snapshot / 'Build').mkdir(exist_ok=True)
    (snapshot / 'Build/SourceCommit.txt').write_text(
        subprocess.check_output(['git', '-C', str(project), 'rev-parse', 'HEAD'], text=True).strip() + '\n')
    (snapshot / 'Build/SourceCommitDate.txt').write_text(
        subprocess.check_output(['git', '-C', str(project), 'show', '-s', '--format=%cI', 'HEAD'], text=True).strip() + '\n')
    (snapshot / 'scripts').mkdir(exist_ok=True)
    shutil.copy2(project / 'scripts/stamp-build-version.py', snapshot / 'scripts/stamp-build-version.py')
subprocess.run([sys.executable, str(project / 'scripts/prepare-eos-guest.py'),
                '--project', str(snapshot), '--engine', str(engine)], check=True)
# Older resumable snapshots may predate the cooked-animation verifier.
if not (snapshot / 'scripts/bake_cooked_first_person.py').is_file():
    shutil.copy2(project / 'scripts/bake_cooked_first_person.py', snapshot / 'scripts/bake_cooked_first_person.py')
if not (snapshot / 'scripts/export-match-stats.py').is_file():
    shutil.copy2(project / 'scripts/export-match-stats.py', snapshot / 'scripts/export-match-stats.py')
uproject = snapshot / 'RespawnAndRage.uproject'
archive = run_dir / 'dist'
logs = run_dir / 'build.log'
arches = 'arm64+x64' if a.architecture == 'universal' else a.architecture
host = 'arm64' if platform.machine() == 'arm64' else 'x64'
commands = [
    [str(engine / 'Engine/Build/BatchFiles/Mac/Build.sh'), 'RespawnAndRageEditor', 'Mac',
     'Development', f'-Project={uproject}', f'-architecture={host}', '-NoHotReloadFromIDE'],
    [str(engine / 'Engine/Binaries/Mac/UnrealEditor.app/Contents/MacOS/UnrealEditor'), str(uproject),
     '-run=pythonscript', f'-script={snapshot / "scripts/bake_cooked_first_person.py"}',
     '-nullrhi', '-unattended', '-nosound', '-NoSplash'],
    [str(engine / 'Engine/Build/BatchFiles/RunUAT.sh'), 'BuildCookRun', f'-project={uproject}',
     '-target=RespawnAndRage', '-platform=Mac', f'-clientconfig={a.configuration}',
     f'-clientarchitecture={arches}', '-nop4', '-unattended', '-utf8output', '-installed',
     # Use ProjectPackagingSettings.MapsToCook; -map overrides that list.
     '-build', '-cook', '-stage', '-package', '-pak', '-iostore',
     '-skipbuildeditor', '-archive', f'-archivedirectory={archive}'],
]
if a.resume:
    commands = commands[1:]  # The Editor module already compiled in this snapshot.
if a.xcode_stdin_workaround:
    # Build under Xcode's documented environment (defers finalization), then run
    # the real finalization outside UBA with a valid stdin. No engine patch.
    game_build = ['env', 'UE_BUILD_FROM_XCODE=1',
                  str(engine / 'Engine/Build/BatchFiles/Mac/Build.sh'),
                  'RespawnAndRage', 'Mac', a.configuration, f'-Project={uproject}',
                  f'-architecture={arches}', '-skipdeploy']
    sync_input = snapshot / f'Intermediate/Build/Mac/{arches}/RespawnAndRage/{a.configuration}/PostBuildSync_RespawnAndRage.json'
    finalize = [str(engine / 'Engine/Build/BatchFiles/RunUBT.sh'), '-Mode=ApplePostBuildSync',
                f'-Input={sync_input}', f'-XmlConfigCache={snapshot / "Intermediate/Build/XmlConfigCache.bin"}',
                f'-remoteini={snapshot}']
    commands[-1] = ['-skipbuild' if x == '-build' else x for x in commands[-1]]
    if not a.recook_only:
        commands[-1:-1] = [game_build, finalize]
if a.recook_only:
    # Cook an explicit map list using the already linked snapshot. Never rebuild
    # the objects intentionally discarded after successful finalization.
    commands = [[('-skipbuild' if x == '-build' else x) for x in commands[-1]]]
spec = importlib.util.spec_from_file_location('stamp_build_version', snapshot / 'scripts/stamp-build-version.py')
version_module = importlib.util.module_from_spec(spec)
spec.loader.exec_module(version_module)
game_version = version_module.stamp(snapshot)
receipt = {'project_head': version_module.source_commit(snapshot), 'game_version': game_version,
           'configuration': a.configuration,
           'engine_version': '5.8.3', 'architecture': arches,
           'xcode_stdin_workaround': a.xcode_stdin_workaround, 'apfs_clones': use_clones, 'project_snapshot': str(snapshot), 'archive': str(archive), 'commands': commands,
           'status': 'running', 'distribution_signing': 'Not configured'}
receipt_path = run_dir / 'receipt.json'
receipt_path.write_text(json.dumps(receipt, indent=2) + '\n')
try:
    with logs.open('a' if a.resume else 'w') as log:
        for command in commands:
            process = subprocess.Popen(command, cwd=snapshot, stdout=log,
                                       stderr=subprocess.STDOUT, stdin=subprocess.DEVNULL, start_new_session=True)
            try:
                while process.poll() is None:
                    if shutil.disk_usage(run_dir).free < 5 * 1024**3:
                        raise RuntimeError('Build stopped to preserve 5 GiB free disk space')
                    time.sleep(5)
                if process.returncode:
                    raise subprocess.CalledProcessError(process.returncode, command)
                if len(command) > 1 and command[1] == 'RespawnAndRageEditor':
                    # Cook loads the linked Editor modules, not their objects.
                    # Retain binaries/headers while freeing completed Editor
                    # PCH/object files before compiling two Game architectures.
                    removed_bytes = 0
                    for base in (snapshot / 'Intermediate', snapshot / 'Plugins'):
                        for path in base.rglob('*'):
                            if (path.is_file() and not path.is_symlink() and
                                '/UnrealEditor/Development/' in str(path) and path.suffix in ('.o', '.gch')):
                                removed_bytes += path.stat().st_size
                                path.unlink()
                    receipt['completed_editor_objects_removed_bytes'] = removed_bytes
                if '-Mode=ApplePostBuildSync' in command:
                    # Finalization has consumed the Game objects for linking
                    # and symbols. BuildCookRun uses -skipbuild on this route.
                    removed_bytes = 0
                    for base in (snapshot / 'Intermediate', snapshot / 'Plugins'):
                        for path in base.rglob('*'):
                            if (path.is_file() and not path.is_symlink() and path.suffix in ('.o', '.gch') and
                                any('/' + target + '/Development/' in str(path)
                                    for target in ('RespawnAndRage', 'UnrealGame'))):
                                removed_bytes += path.stat().st_size
                                path.unlink()
                    receipt['finalized_game_objects_removed_bytes'] = removed_bytes
            finally:
                if process.poll() is None:
                    os.killpg(process.pid, signal.SIGTERM)
                    try:
                        process.wait(timeout=15)
                    except subprocess.TimeoutExpired:
                        os.killpg(process.pid, signal.SIGKILL)
                        process.wait()
    apps = [app for app in archive.rglob('*.app') if app.name.startswith('RespawnAndRage')]
    if not apps:
        raise RuntimeError('Packaged RespawnAndRage.app missing')
    for app in apps:
        with (app / 'Contents/Info.plist').open('rb') as info:
            bundle_info = plistlib.load(info)
            executable = bundle_info['CFBundleExecutable']
        if not bundle_info.get('NSHighResolutionCapable'):
            raise RuntimeError('Packaged app does not enable Retina; check project Info.Template.plist')
        if not (app / 'Contents/Resources/AppIcon.icns').is_file():
            raise RuntimeError('Packaged app icon is missing')
        packaged_version = app / 'Contents/UE/RespawnAndRage/Build/GameVersion.txt'
        if not packaged_version.is_file() or packaged_version.read_text().strip() != (snapshot / 'Build/GameVersion.txt').read_text().strip():
            raise RuntimeError('Packaged game version is missing or differs from the build stamp')
        subprocess.run([sys.executable, str(Path(__file__).with_name('stage-match-stats.py')),
                        '--archive', str(app),
                        *(arg for arch in arches.split('+') for arg in ('--target', f'mac-{arch}'))],
                       check=True)
        binary = app / 'Contents/MacOS' / executable
        actual = subprocess.check_output(['lipo', '-archs', str(binary)], text=True).strip()
        for required in arches.split('+'):
            required = 'x86_64' if required == 'x64' else required
            if required not in actual.split():
                raise RuntimeError(f'Packaged binary missing {required}: {actual}')
        receipt['verified_binary_architectures'] = actual
        if a.configuration == 'Development':
            # Menu startup misses animation failures that only appear after
            # spawning pistol-equipped players/bots in a cooked executable.
            subprocess.run([sys.executable, str(Path(__file__).with_name('test-packaged-runtime.py')),
                            '--executable', str(binary), '--all-maps', '--output', str(run_dir / 'runtime-smoke')], check=True)
            receipt['packaged_runtime_smoke'] = str(run_dir / 'runtime-smoke/maps.json')
            for shader_model in ('sm5', 'sm6'):
                subprocess.run([sys.executable, str(Path(__file__).with_name('test-packaged-runtime.py')),
                                '--executable', str(binary), '--render', '--shader-model', shader_model,
                                '--output', str(run_dir / 'runtime-smoke')], check=True)
            receipt['packaged_metal_smoke'] = {
                model: str(run_dir / ('runtime-smoke/metal-' + model + '.json')) for model in ('sm5', 'sm6')}
    receipt['status'] = 'packaged'
    receipt['game_version'] = (snapshot / 'Build/GameVersion.txt').read_text().strip()
except BaseException:
    receipt['status'] = 'failed'
    raise
finally:
    receipt_path.write_text(json.dumps(receipt, indent=2) + '\n')
    print('Build receipt: ' + str(receipt_path), flush=True)
