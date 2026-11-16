#!/usr/bin/env python3
"""Preflight, isolated Mac build, Developer ID signing, notarization and itch upload.

Credentials stay in Keychain/Butler's identity file. --check never builds,
signs, submits or uploads. Close Unreal before starting a new build.
"""
import argparse
import importlib.util
import json
import os
from pathlib import Path
import platform
import plistlib
import re
import shlex
import shutil
import subprocess
import sys
import time
import uuid

PROJECT = Path(__file__).resolve().parents[1]
SCRIPTS = Path(__file__).resolve().parent
GAME = 'aaggeeaa/respawn-and-rage'
CHANNELS = {'universal': 'mac-universal', 'arm64': 'mac-arm64', 'x64': 'mac-x86_64'}
GIB = 1024 ** 3


def helper(name):
    spec = importlib.util.spec_from_file_location(name.replace('-', '_'), SCRIPTS / (name + '.py'))
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def run(command, log=None, check=True):
    command = list(map(str, command))
    print('+ ' + shlex.join(command), flush=True)
    if log is None:
        result = subprocess.run(command, capture_output=True, text=True)
        if result.stdout:
            print(result.stdout, end='', flush=True)
        if result.stderr:
            print(result.stderr, end='', file=sys.stderr, flush=True)
    else:
        # Child tools write their full output here; the build also has build.log.
        with log.open('a') as stream:
            stream.write('+ ' + shlex.join(command) + '\n')
            stream.flush()
            result = subprocess.run(command, stdout=stream, stderr=subprocess.STDOUT)
    if check and result.returncode:
        raise RuntimeError(f'{Path(command[0]).name} failed (exit {result.returncode})' +
                           (f'; inspect {log}' if log else ''))
    return result


def save(path, data):
    temporary = path.with_suffix('.json.tmp')
    temporary.write_text(json.dumps(data, indent=2) + '\n')
    temporary.replace(path)


def disk_space(path, minimum):
    nearest = path
    while not nearest.exists():
        nearest = nearest.parent
    free = shutil.disk_usage(nearest).free
    print(f'Space on {nearest}: {free / GIB:.1f} GiB free, {minimum / GIB:.1f} GiB required')
    if free < minimum:
        raise RuntimeError(f'Free at least {minimum / GIB:.1f} GiB on {nearest}, or use --workspace on a larger disk')


def butler_arguments(args):
    return [args.butler] + (['--identity', args.butler_identity] if args.butler_identity else [])


def discover_butler(args):
    """Reuse installed CLI/app binaries and the existing builder login, without reading keys."""
    if args.butler == 'butler' and not shutil.which('butler'):
        legacy = PROJECT.parent / 'unreal-builder/.work/butler/butler'
        bundled = Path.home() / 'Library/Application Support/itch/broth/butler/versions'
        candidates = [legacy] + sorted(bundled.glob('*/butler'), key=lambda p: (
            tuple(int(n) for n in re.findall(r'\d+', p.parent.name))), reverse=True)
        args.butler = next((str(p.resolve()) for p in candidates if p.is_file() and os.access(p, os.X_OK)), 'butler')
    if args.butler_identity is None and not os.environ.get('BUTLER_API_KEY', '').strip():
        candidates = [Path.home() / 'Library/Application Support/itch/butler_creds',
                      Path.home() / '.config/itch/butler_creds',
                      PROJECT.parent / 'unreal-builder/.work/itch-identity']
        args.butler_identity = next((p.resolve() for p in candidates if p.is_file() and p.stat().st_size), None)


def check_butler(args):
    run([args.butler, 'version'])
    identity = args.butler_identity or Path.home() / 'Library/Application Support/itch/butler_creds'
    if not os.environ.get('BUTLER_API_KEY', '').strip() and (not identity.is_file() or not identity.stat().st_size):
        raise RuntimeError('Run butler login first (with --identity PATH if selected), or set BUTLER_API_KEY in the environment')
    run(butler_arguments(args) + ['status', GAME])


def check_plugins(engine):
    references = json.loads((PROJECT / 'RespawnAndRage.uproject').read_text())['Plugins']
    local = list((PROJECT / 'Plugins').rglob('*.uplugin'))
    installed = list((engine / 'Engine/Plugins').rglob('*.uplugin'))
    for reference in references:
        if not reference.get('Enabled'):
            continue
        name = reference['Name']
        matches = [p for p in local if p.stem == name] or [p for p in installed if p.stem == name]
        if len(matches) != 1:
            raise RuntimeError('Missing or ambiguous plugin: ' + name)
        descriptor = matches[0]
        data = json.loads(descriptor.read_text())
        if data.get('SupportedTargetPlatforms') and 'Mac' not in data['SupportedTargetPlatforms']:
            raise RuntimeError('Plugin does not support Mac: ' + name)
        if (descriptor in local or 'Marketplace' in descriptor.parts) and data.get('Modules'):
            if not (descriptor.parent / 'Source').is_dir():
                raise RuntimeError('Install plugin source for the Game build: ' + name)
        if name == 'PCGExtendedToolkit' and data.get('VersionName') != '0.79.2':
            raise RuntimeError('PCGExtendedToolkit 0.79.2 required')


def check_build(args, test):
    reports = []

    def engine():
        for relative in ('Engine/Build/InstalledBuild.txt', 'Engine/Build/BatchFiles/Mac/Build.sh',
                         'Engine/Build/BatchFiles/RunUAT.sh', 'Engine/Binaries/Mac/UnrealEditor.app',
                         'Engine/Binaries/DotNET/UnrealBuildTool/UnrealBuildTool.dll'):
            if not (args.engine / relative).exists():
                raise RuntimeError('Missing ' + relative + '; verify UE 5.8.3 in Epic Launcher')
        # Reuse the packager's exact UE/Xcode/APFS checks and disk estimate.
        result = run([sys.executable, SCRIPTS / 'package-macos.py', '--check',
                      '--engine', args.engine, '--workspace', args.workspace,
                      '--architecture', args.architecture], check=False)
        reports.extend(json.loads(line) for line in result.stdout.splitlines() if line.startswith('{'))
        if result.returncode not in (0, 2) or not reports:
            raise RuntimeError('Mac build environment check failed; inspect the diagnostic above')

    def metal():
        command = ['xcrun', '--sdk', 'macosx', 'metal', '-v']
        if run(command, check=False).returncode:
            # Xcode 26+ can have the Metal asset installed but not mounted or
            # associated with the selected Xcode after an update/reboot.
            run(['xcodebuild', '-showComponent', 'MetalToolchain'], check=False)
            if run(command, check=False).returncode:
                raise RuntimeError('Metal compiler unavailable; install the Xcode Metal Toolchain '
                                   '(xcodebuild -downloadComponent MetalToolchain)')

    def dotnet():
        host = 'mac-arm64' if platform.machine() == 'arm64' else 'mac-x64'
        runtimes = list((args.engine / 'Engine/Binaries/ThirdParty/DotNet').glob('*/' + host + '/dotnet'))
        if not runtimes:
            raise RuntimeError('Bundled .NET runtime missing; verify UE 5.8.3 in Epic Launcher')
        run([runtimes[0], '--info'])

    def branding():
        for relative in ('Branding/background.jpg', 'Branding/icon.png', 'Branding/Credits.txt',
                         'Branding/Fonts/Teko/Fonts/Teko-SemiBold.ttf',
                         'Branding/Fonts/Teko/ThirdPartyLicenses/Teko-OFL.txt', 'Branding/Fonts/Teko/CREDITS.txt',
                         'Build/Mac/Resources/Info.Template.plist', 'Build/Mac/Application.icns'):
            if not (PROJECT / relative).is_file():
                raise RuntimeError('Missing runtime branding: ' + relative)

    def space():
        if not reports:
            raise RuntimeError('Fix the UE/Xcode check first to measure the APFS/build space requirement')
        minimum = (reports[-1]['minimum_free_gib'] + 5) * GIB
        if not args.check:
            nearest = args.workspace
            while not nearest.exists():
                nearest = nearest.parent
            helper('clean-project').reclaim_build_space(PROJECT, nearest, minimum)
        disk_space(args.workspace, minimum)
        # Global DDC and SDK tools can still write to the startup volume.
        disk_space(PROJECT, 5 * GIB)
        disk_space(Path.home(), 5 * GIB)

    test('UE 5.8.3 Installed Build and APFS probe', engine)
    test('Full Xcode first-launch setup and macOS SDK', lambda: (
        run(['xcodebuild', '-checkFirstLaunchStatus']), run(['xcrun', '--sdk', 'macosx', '--show-sdk-path'])))
    test('Metal shader compiler', metal)
    test('Bundled .NET runtime', dotnet)
    test('Enabled plugins and Game target source', lambda: check_plugins(args.engine))
    test('Runtime branding, icon and fonts', branding)
    test('Build and notarization disk space', space)


def package(run_dir):
    """Read a successful isolated build; never select a stale app by timestamp."""
    build = run_dir / 'build'
    receipt = json.loads((build / 'receipt.json').read_text())
    if receipt.get('status') != 'packaged':
        raise RuntimeError('No completed package in this release; start a new release for a failed build')
    archive = build / 'dist'
    if Path(receipt['archive']).resolve() != archive.resolve():
        raise RuntimeError('Build receipt archive does not match this release')
    # Xcode retains a diagnostic archive containing another copy of the app.
    apps = [p for p in archive.rglob('RespawnAndRage.app') if not p.is_symlink()
            and not any(part.endswith('.xcarchive') for part in p.relative_to(archive).parts[:-1])]
    if len(apps) != 1:
        raise RuntimeError('Expected exactly one packaged RespawnAndRage.app')
    app = apps[0]
    if not app.resolve().is_relative_to(archive.resolve()):
        raise RuntimeError('Packaged app escapes the archive')
    if any(p != app and p.name != '.DS_Store' for p in app.parent.iterdir()):
        raise RuntimeError('Upload directory must contain only the packaged app')
    version = (app / 'Contents/UE/RespawnAndRage/Build/GameVersion.txt').read_text().strip()
    if not re.fullmatch(r'\d{2}\.\d{2}\.\d{2}\.[0-9a-f]{8}', version) or version != receipt['game_version']:
        raise RuntimeError('Packaged version does not match the build receipt')
    architecture = {'arm64+x64': 'universal', 'arm64': 'arm64', 'x64': 'x64'}[receipt['architecture']]
    with (app / 'Contents/Info.plist').open('rb') as stream:
        info = plistlib.load(stream)
    if not info.get('NSHighResolutionCapable') or not (app / 'Contents/Resources/AppIcon.icns').is_file():
        raise RuntimeError('App icon or Retina metadata missing')
    binary = app / 'Contents/MacOS' / info['CFBundleExecutable']
    arches = run(['lipo', '-archs', binary]).stdout.split()
    required = {'arm64', 'x86_64'} if architecture == 'universal' else {architecture.replace('x64', 'x86_64')}
    if set(arches) != required:
        raise RuntimeError('Packaged executable architectures differ from the build receipt')
    return app, version, architecture


def notarization_space(app, run_dir, method='notarytool'):
    size = sum(p.stat().st_size for p in app.rglob('*') if p.is_file() and not p.is_symlink())
    # ZIP can be almost as large as the bundle; leave another GiB for metadata.
    disk_space(run_dir, size * (2 if method in ('auto', 'xcode') else 1) + GIB)


def verify_distribution(app):
    receipt = json.loads((app.parent.parent / (app.stem + '-notarization.json')).read_text())
    if receipt.get('status') != 'accepted-and-gatekeeper-verified' or Path(receipt['app']).resolve() != app.resolve():
        raise RuntimeError('A successful notarization receipt for this app is required before upload')
    run(['codesign', '--verify', '--deep', '--strict', '--verbose=2', app])
    details = run(['codesign', '-d', '--verbose=4', app])
    if 'Authority=Developer ID Application:' not in details.stderr or '(runtime)' not in details.stderr:
        raise RuntimeError('The app must have a Developer ID signature with hardened runtime')
    run(['xcrun', 'stapler', 'validate', app])
    run(['spctl', '--assess', '--type', 'execute', '--verbose=2', app])


def preflight(args):
    if platform.system() != 'Darwin':
        raise RuntimeError('Run this script on macOS')
    if not (args.resume or args.upload_only):
        if not args.check:
            helper('clean-project').ensure_idle()
        cleanup = helper('build_cleanup')
        cleanup.cleanup_old_builds(cleanup.build_roots(args.workspace, PROJECT), dry_run=args.check)
    if not args.no_upload:
        discover_butler(args)
    failures = []

    def test(name, action):
        try:
            action()
            print('PASS: ' + name, flush=True)
        except (OSError, ValueError, RuntimeError, subprocess.CalledProcessError) as error:
            failures.append(name + ': ' + str(error))
            print('FAIL: ' + failures[-1], file=sys.stderr, flush=True)

    def tools():
        required = ['xcrun', 'codesign', 'security', 'ditto', 'spctl', 'lipo']
        if not (args.resume or args.upload_only):
            required += ['git', 'xcodebuild', 'python3']
        if not args.no_upload:
            required += [args.butler]
        missing = [name for name in required if not shutil.which(name)]
        if missing:
            raise RuntimeError('Install/add to PATH: ' + ', '.join(missing))

    test('Required command line tools (full Xcode, Butler)', tools)
    if not (args.resume or args.upload_only):
        test('Git and LFS', lambda: (run(['git', 'lfs', 'version']), run(['git', '-C', PROJECT, 'rev-parse', '--verify', 'HEAD'])))
        test('Unreal Editor/build tools closed', lambda: helper('clean-project').ensure_idle())
        check_build(args, test)
        # --init-assets is intentionally performed only after every other gate.
        if not args.init_assets:
            test('Private asset mounts and LFS files', lambda: run([sys.executable, SCRIPTS / 'setup-assets.py', '--check']))
    else:
        def existing_package():
            app, _, _ = package(args.resume or args.upload_only)
            if args.upload_only:
                verify_distribution(app)
            else:
                notarization_space(app, args.resume, args.notarization_method)
        test('Existing package' + (' and distribution signature/ticket' if args.upload_only else ''), existing_package)
    if not args.upload_only:
        def signing():
            args.identity, args.notarization_method = helper('notarize-macos').resolve_credentials(
                args.identity, args.keychain_profile, args.notarization_method)
            print('Signing identity: ' + args.identity)
            print('Notarization method: ' + args.notarization_method)
            if args.notarization_method == 'xcode':
                print('Xcode account is configured; authentication is checked only during upload. '
                      'If Xcode reports No Accounts / missing Xcode-Token, sign in again in '
                      'Xcode > Settings > Accounts, then use --resume with the release directory.')
        test('Developer ID private key and notarization configuration (notarytool/Xcode)', signing)
    if not args.no_upload:
        test('Butler login and itch.io access', lambda: check_butler(args))
    if failures:
        raise RuntimeError('Preflight failed:\n  ' + '\n  '.join(failures))


def release(args):
    preflight(args)
    if args.check:
        print('Preflight complete; no build, signing, submission or upload performed.')
        return
    if args.init_assets:
        run([sys.executable, SCRIPTS / 'setup-assets.py', '--init'])
    run_dir = args.resume or args.upload_only or args.workspace / ('release-' + time.strftime('%Y%m%d-%H%M%S') + '-' + uuid.uuid4().hex[:8])
    if not (args.resume or args.upload_only):
        run_dir.mkdir(parents=True, exist_ok=False)
    receipt_path = run_dir / 'release.json'
    receipt = json.loads(receipt_path.read_text()) if receipt_path.exists() else {'run_directory': str(run_dir)}
    print('Release directory:', run_dir, flush=True)
    try:
        if not (args.resume or args.upload_only):
            receipt['status'] = 'building'
            save(receipt_path, receipt)
            run([sys.executable, SCRIPTS / 'package-macos.py', '--engine', args.engine,
                 '--workspace', run_dir, '--run-directory', run_dir / 'build',
                 '--architecture', args.architecture, '--configuration', args.configuration], log=run_dir / 'release.log')
        app, version, architecture = package(run_dir)
        receipt.pop('error', None)
        receipt.pop('failed_at', None)
        receipt.update(app=str(app), game_version=version, channel=CHANNELS[architecture])
        if not args.upload_only:
            receipt['status'] = 'notarizing'
            save(receipt_path, receipt)
            notarization_space(app, run_dir, args.notarization_method)
            scratch = run_dir / 'scratch'
            scratch.mkdir(exist_ok=True)
            run([sys.executable, SCRIPTS / 'notarize-macos.py', app, '--identity', args.identity,
                 '--keychain-profile', args.keychain_profile, '--method', args.notarization_method,
                 '--scratch-directory', scratch], log=run_dir / 'notarization.log')
        verify_distribution(app)
        receipt['status'] = 'ready'
        save(receipt_path, receipt)
        if not args.no_upload:
            receipt['status'] = 'uploading'
            save(receipt_path, receipt)
            run(butler_arguments(args) + ['push', app.parent, GAME + ':' + CHANNELS[architecture],
                                         '--userversion', version, '--ignore', '.DS_Store'], log=run_dir / 'upload.log')
            receipt['status'] = 'uploaded'
    except BaseException as error:
        receipt['failed_at'] = receipt.get('status', 'package-validation')
        receipt['status'] = 'failed'
        receipt['error'] = str(error)
        raise
    finally:
        save(receipt_path, receipt)
        print('Release receipt:', receipt_path, flush=True)
    print('Release ' + receipt['status'] + ': ' + version, flush=True)
    if receipt['status'] == 'uploaded':
        helper('build_cleanup').remove_uploaded_build(run_dir, PROJECT)


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--engine', type=Path, default=Path('/Users/Shared/Epic Games/UE_5.8'))
    parser.add_argument('--workspace', type=Path, default=PROJECT.parent / 'builds/macos')
    parser.add_argument('--architecture', choices=tuple(CHANNELS), default='universal')
    parser.add_argument('--configuration', choices=('Development', 'Shipping'), default='Development')
    parser.add_argument('--identity', help='Developer ID Application name; auto-selects only if exactly one is valid')
    parser.add_argument('--keychain-profile', default='RespawnAndRage')
    parser.add_argument('--notarization-method', choices=('auto', 'notarytool', 'xcode'), default='auto',
                        help='Use the Keychain profile if available, otherwise the existing Xcode account')
    parser.add_argument('--butler', default='butler', help='Butler executable on PATH or absolute path')
    parser.add_argument('--butler-identity', type=Path, help='Alternate private Butler identity file')
    parser.add_argument('--init-assets', action='store_true', help='Fetch pinned private submodules/LFS and create mounts')
    parser.add_argument('--check', action='store_true', help='Preflight only; Apple/Butler authentication uses read-only online calls')
    parser.add_argument('--no-upload', action='store_true', help='Build, sign, notarize and verify without itch upload')
    retries = parser.add_mutually_exclusive_group()
    retries.add_argument('--resume', type=Path, help='Reuse a completed release build and retry signing/notarization/upload')
    retries.add_argument('--upload-only', type=Path, help='Verify and upload an already notarized release, without rebuilding/signing')
    args = parser.parse_args(argv)
    if sys.version_info < (3, 10):
        parser.error('Mac release requires Python 3.10+; Windows helper supports Python 3.9+')
    if args.init_assets and (args.check or args.resume or args.upload_only):
        parser.error('--init-assets cannot be combined with --check/--resume/--upload-only')
    if args.upload_only and args.no_upload:
        parser.error('--upload-only cannot be combined with --no-upload')
    for name in ('engine', 'workspace', 'resume', 'upload_only', 'butler_identity'):
        value = getattr(args, name)
        if value is not None:
            setattr(args, name, value.expanduser().resolve())
    if '/' in args.butler:
        args.butler = str(Path(args.butler).expanduser().resolve())
    for path in filter(None, (args.workspace, args.resume, args.upload_only)):
        if path.is_relative_to(PROJECT) or PROJECT.is_relative_to(path):
            parser.error('Use build/release directories outside the source checkout')
    try:
        helper('stamp-build-version').ensure_clean(PROJECT)
        release(args)
    except (OSError, ValueError, KeyError, RuntimeError, subprocess.CalledProcessError) as error:
        print(str(error), file=sys.stderr)
        return 1
    return 0


if __name__ == '__main__':
    sys.exit(main())
