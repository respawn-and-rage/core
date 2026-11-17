"""Sign a packaged Mac app, notarize it, staple the ticket and verify Gatekeeper.

Requires a Developer ID Application identity and notarytool profile or Xcode account.
Credentials stay in Keychain. Does not upload anything to itch.io.
"""
import argparse
from datetime import datetime, timezone
import importlib.util
import json
import os
from pathlib import Path
import plistlib
import re
import shutil
import shlex
import subprocess
import sys
import tempfile
import time


MACHO = {bytes.fromhex(x) for x in (
    'feedface', 'cefaedfe', 'feedfacf', 'cffaedfe',
    'cafebabe', 'bebafeca', 'cafebabf', 'bfbafeca')}
XCODE_AUTH_FAILURE = r'No Accounts|Xcode-Token|invalid credentials'


def run(*args, **kwargs):
    kwargs.setdefault('check', True)
    return subprocess.run(list(map(str, args)), **kwargs)


def is_macho(path):
    if path.is_symlink() or not path.is_file():
        return False
    with path.open('rb') as stream:
        return stream.read(4) in MACHO


def check_credentials(identity, keychain_profile, sign_only=False):
    identities = run('security', 'find-identity', '-v', '-p', 'codesigning', capture_output=True, text=True).stdout
    available = re.findall(r'"(Developer ID Application:[^"\n]+)"', identities)
    if identity is None and len(set(available)) == 1:
        identity = available[0]
    if not identity or identity not in available:
        raise RuntimeError('Install a valid Developer ID Application certificate with its private key in Keychain; '
                           'if several are installed, choose one with --identity "Developer ID Application: Name (TEAMID)"')
    if not sign_only:
        try:
            run('xcrun', 'notarytool', 'history', '--keychain-profile', keychain_profile,
                '--output-format', 'json', stdout=subprocess.DEVNULL)
        except subprocess.CalledProcessError as error:
            raise RuntimeError('Notarytool credentials unavailable or authentication failed; configure/check '
                               'the Keychain profile with: xcrun notarytool store-credentials ' +
                               str(keychain_profile)) from error
    return identity


def xcode_account_available():
    # This preference contains public account identifiers, not Xcode tokens.
    # Never inspect/extract Xcode's private authentication material.
    result = run('defaults', 'read', 'com.apple.dt.Xcode', 'DVTDeveloperAccountManagerAppleIDLists',
                 capture_output=True, text=True, check=False)
    if result.returncode:
        return False
    # Xcode versions use different namespaces/identifier formats (IDE.Prod,
    # Xcode.Account.AppleID, etc.). Parse the public lists rather than assuming
    # that an account identifier is a UUID.
    converted = run('plutil', '-convert', 'json', '-o', '-', '-', input=result.stdout,
                    capture_output=True, text=True, check=False)
    if converted.returncode:
        return False
    data = json.loads(converted.stdout)
    return isinstance(data, dict) and any(isinstance(v, list) and v for v in data.values())


def resolve_credentials(identity, keychain_profile, method='auto'):
    identity = check_credentials(identity, keychain_profile, sign_only=True)
    if method != 'xcode':
        result = run('xcrun', 'notarytool', 'history', '--keychain-profile', keychain_profile,
                     '--output-format', 'json', capture_output=True, text=True, check=False)
        if result.returncode == 0:
            return identity, 'notarytool'
        if method == 'notarytool':
            raise RuntimeError('Notarytool profile unavailable: ' + str(keychain_profile) +
                               '; configure it locally or select --notarization-method xcode')
    if not xcode_account_available():
        raise RuntimeError('Configure an Apple Developer account in Xcode Settings > Accounts, '
                           'or a notarytool Keychain profile. No credentials are read from Xcode.')
    return identity, 'xcode'


def copy_app(source, destination):
    spec = importlib.util.spec_from_file_location('apfs_snapshot', Path(__file__).with_name('apfs_snapshot.py'))
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    shutil.copytree(source, destination, symlinks=True, copy_function=module.copy_file)


def xcode_failure(stage, diagnostic, archive):
    if re.search(XCODE_AUTH_FAILURE, diagnostic, re.I):
        message = ('Xcode authentication failed. Open Xcode > Settings > Accounts and sign in again '
                   'to refresh the Apple Developer account, then retry with release-macos.py --resume '
                   'and the existing release directory; no rebuild is needed.')
    else:
        message = 'Xcode ' + stage + ' failed.'
    return RuntimeError(message + '\nArchive retained at ' + str(archive) + '\n' + diagnostic[-3000:])


def cleanup_xcode_staging(temp_root, log_bundle, archive, app_name):
    """Find the staging directory Xcode recorded for this exact archive."""
    if log_bundle.is_symlink():
        return
    pipeline_log = log_bundle / 'IDEDistributionPipeline.log'
    if not pipeline_log.is_file() or pipeline_log.is_symlink():
        return
    product = archive / 'Products/Applications' / app_name
    marker = "Running /usr/bin/ditto "
    with pipeline_log.open(encoding='utf-8', errors='replace') as stream:
        for line in stream:
            if marker not in line or str(product) not in line:
                continue
            try:
                arguments = shlex.split(line.split(marker, 1)[1])
            except ValueError:
                continue
            if len(arguments) < 2 or Path(arguments[-2]) != product:
                continue
            destination = Path(arguments[-1])
            staging = destination.parents[2] if len(destination.parents) >= 3 else None
            if (staging is None or
                    destination != staging / 'Root/Applications' / app_name or
                    staging.parent.resolve() != temp_root or
                    not re.fullmatch(r'XcodeDistPipeline[.]~{3}[A-Za-z0-9]+', staging.name)):
                continue
            if staging.is_dir() and not staging.is_symlink() and staging.stat().st_uid == os.getuid():
                shutil.rmtree(staging)
                print('Removed Xcode distribution staging directory: ' + str(staging), flush=True)


def run_xcode_distribution(scratch, archive, app_name, action, *args):
    # Xcode may use the system temp directory despite TMPDIR.
    temp_root = Path(tempfile.gettempdir()).resolve()
    pattern = Path(app_name).stem + '_*.xcdistributionlogs'
    existing_logs = set(temp_root.glob(pattern))
    with tempfile.TemporaryDirectory(prefix='xcode-dist-', dir=scratch) as temporary:
        result = run('xcodebuild', action, '-archivePath', archive, *args,
                     env={**os.environ, 'TMPDIR': temporary + os.sep},
                     capture_output=True, text=True, check=False)
        for log_bundle in set(temp_root.glob(pattern)) - existing_logs:
            try:
                cleanup_xcode_staging(temp_root, log_bundle, archive, app_name)
            except OSError as error:
                print(f'Warning: could not clean Xcode distribution staging: {error}', file=sys.stderr)
        return result


def notarize_with_xcode(app, identity, scratch, receipt, timeout):
    """Use Xcode's existing account through its supported archive distribution CLI."""
    info = plistlib.loads((app / 'Contents/Info.plist').read_bytes())
    team = re.search(r'\(([A-Z0-9]{10})\)$', identity)
    if not team:
        raise RuntimeError('Cannot determine the Developer ID team from the signing identity')
    archive = app.parent.parent / (app.stem + '.xcarchive')
    # Retain failed attempts; a retry must not silently overwrite their archive.
    if archive.exists():
        archive = archive.with_name(app.stem + '-' + str(time.time_ns()) + '.xcarchive')
    product = archive / 'Products/Applications' / app.name
    product.parent.mkdir(parents=True)
    copy_app(app, product)
    arches = run('lipo', '-archs', app / 'Contents/MacOS' / info['CFBundleExecutable'],
                 capture_output=True, text=True).stdout.split()
    (archive / 'Info.plist').write_bytes(plistlib.dumps({
        'ArchiveVersion': 2, 'CreationDate': datetime.now(timezone.utc).replace(tzinfo=None),
        'Name': app.stem, 'SchemeName': app.stem,
        'ApplicationProperties': {'ApplicationPath': 'Applications/' + app.name,
            'Architectures': arches, 'CFBundleIdentifier': info['CFBundleIdentifier'],
            'CFBundleShortVersionString': info.get('CFBundleShortVersionString', '1.0'),
            'CFBundleVersion': info.get('CFBundleVersion', '1'), 'SigningIdentity': identity, 'Team': team[1]}}))
    options = scratch / 'ExportOptions.plist'
    options.write_bytes(plistlib.dumps({'method': 'developer-id', 'destination': 'upload',
        'signingStyle': 'manual', 'signingCertificate': identity, 'teamID': team[1],
        'stripSwiftSymbols': False, 'manageAppVersionAndBuildNumber': False}))
    receipt['submission'] = {'method': 'xcode', 'archive': str(archive), 'status': 'uploading'}
    result = run_xcode_distribution(scratch, archive, app.name, '-exportArchive',
                                    '-exportOptionsPlist', options, '-allowProvisioningUpdates')
    if result.stdout:
        print(result.stdout, end='', flush=True)
    if result.stderr:
        print(result.stderr, end='', file=sys.stderr, flush=True)
    if result.returncode:
        raise xcode_failure('archive upload', (result.stdout or '') + (result.stderr or ''), archive)
    receipt['submission']['status'] = 'processing'
    exported = scratch / 'notarized-export'
    deadline = time.monotonic() + timeout
    while True:
        if exported.exists():
            shutil.rmtree(exported)
        result = run_xcode_distribution(scratch, archive, app.name, '-exportNotarizedApp',
                                        '-exportPath', exported)
        if result.returncode == 0:
            break
        diagnostic = (result.stdout or '') + (result.stderr or '')
        if re.search(XCODE_AUTH_FAILURE + '|rejected', diagnostic, re.I):
            raise xcode_failure('notarization', diagnostic, archive)
        if not re.search(r'not yet|in progress|processing|not been notarized|not notarized', diagnostic, re.I):
            raise xcode_failure('notarized export', diagnostic, archive)
        if time.monotonic() >= deadline:
            raise RuntimeError('Xcode did not export an accepted notarized app within the timeout. '
                               'Archive retained at ' + str(archive) + '\n' + diagnostic[-3000:])
        print('Waiting for Apple notarization through Xcode...', flush=True)
        time.sleep(15)
    apps = [p for p in exported.rglob(app.name) if p.is_dir() and not p.is_symlink()]
    if len(apps) != 1:
        raise RuntimeError('Xcode did not export exactly one notarized app')
    final = apps[0]
    run('xcrun', 'stapler', 'validate', final)
    run('codesign', '--verify', '--deep', '--strict', '--verbose=2', final)
    run('spctl', '--assess', '--type', 'execute', '--verbose=2', final)
    version = Path('Contents/UE/RespawnAndRage/Build/GameVersion.txt')
    if (final / version).read_bytes() != (app / version).read_bytes():
        raise RuntimeError('Xcode export changed the packaged game version')
    # Prepare on the app volume, then swap only the verified export into dist.
    prepared = archive.with_suffix('.final.app')
    backup = archive.with_suffix('.original.app')
    copy_app(final, prepared)
    os.replace(app, backup)
    try:
        os.replace(prepared, app)
    except BaseException:
        os.replace(backup, app)
        raise
    shutil.rmtree(backup)
    receipt['submission']['status'] = 'exported-notarized-app'


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('app', type=Path)
    parser.add_argument('--identity', required=True, help='Full Developer ID Application identity name')
    parser.add_argument('--keychain-profile')
    parser.add_argument('--method', choices=('notarytool', 'xcode'), default='notarytool')
    parser.add_argument('--timeout-seconds', type=int, default=1800, help='Xcode notarized export timeout')
    parser.add_argument('--sign-only', action='store_true', help='Sign and verify locally; notarization will be done through Xcode Organizer')
    parser.add_argument('--check', action='store_true', help='Check prerequisites without changing the app or submitting it')
    parser.add_argument('--scratch-directory', type=Path, help='Existing directory for the temporary submission ZIP (e.g. the build SSD)')
    args = parser.parse_args()
    if args.timeout_seconds <= 0:
        parser.error('--timeout-seconds must be positive')
    if not args.sign_only and args.method == 'notarytool' and not args.keychain_profile:
        parser.error('--keychain-profile is required unless --sign-only is selected')
    app = args.app.resolve()
    if app.suffix != '.app' or not (app / 'Contents/Info.plist').is_file():
        parser.error('Pass the final packaged .app')
    if args.scratch_directory and not args.scratch_directory.is_dir():
        parser.error('--scratch-directory must exist')
    try:
        if args.sign_only or args.method == 'notarytool':
            check_credentials(args.identity, args.keychain_profile, args.sign_only)
        else:
            resolve_credentials(args.identity, args.keychain_profile, 'xcode')
    except RuntimeError as error:
        parser.error(str(error))
    if args.check:
        if not args.sign_only and args.method == 'xcode':
            print('Signing identity and Xcode account configuration ready; '
                  'Xcode authentication is checked only during upload.')
        else:
            print('Signing identity ready' if args.sign_only else 'Signing identity and notarization credentials ready')
        return
    # Keep dist clean: the itch uploader accepts only the packaged app there.
    receipt_path = app.parent.parent / (app.stem + '-notarization.json')
    receipt = {'app': str(app), 'identity': args.identity, 'status': 'signing'}
    try:
        with tempfile.TemporaryDirectory(prefix='rr-notarize-', dir=args.scratch_directory) as scratch:
            scratch = Path(scratch)
            entitlements = scratch / 'entitlements.plist'
            # No debugger/get-task-allow entitlement in a distributed app.
            entitlements.write_bytes(plistlib.dumps({}))
            code = [p for p in app.rglob('*') if is_macho(p)]
            bundles = [p for p in app.rglob('*') if p.is_dir() and not p.is_symlink()
                       and p.suffix in ('.app', '.framework', '.xpc', '.appex', '.bundle')
                       and ((p / 'Contents/Info.plist').is_file() or p.suffix == '.framework')]
            # Sign nested code before its enclosing bundle. Do not use --deep
            # for signing: every embedded Mach-O gets an explicit signature.
            for path in sorted(set(code + bundles), key=lambda p: len(p.parts), reverse=True) + [app]:
                run('codesign', '--force', '--sign', args.identity, '--timestamp',
                    '--options', 'runtime', '--entitlements', entitlements, path)
            run('codesign', '--verify', '--deep', '--strict', '--verbose=2', app)
            if args.sign_only:
                receipt['status'] = 'signed-not-yet-notarized'
                return
            receipt['status'] = 'submitting'
            if args.method == 'xcode':
                notarize_with_xcode(app, args.identity, scratch, receipt, args.timeout_seconds)
            else:
                archive = scratch / 'submission.zip'
                run('ditto', '-c', '-k', '--keepParent', app, archive)
                submission = run('xcrun', 'notarytool', 'submit', archive,
                    '--keychain-profile', args.keychain_profile, '--wait',
                    '--output-format', 'json', capture_output=True, text=True, check=False)
                receipt['submission'] = json.loads(submission.stdout)
                if submission.returncode or receipt['submission'].get('status') != 'Accepted':
                    submission_id = receipt['submission'].get('id')
                    if submission_id:
                        run('xcrun', 'notarytool', 'log', submission_id,
                            '--keychain-profile', args.keychain_profile,
                            receipt_path.with_name(app.stem + '-notary-log.json'))
                    raise RuntimeError('Apple did not accept the submission; inspect notarization receipt/log')
            run('xcrun', 'stapler', 'staple', app)
            run('xcrun', 'stapler', 'validate', app)
            run('codesign', '--verify', '--deep', '--strict', '--verbose=2', app)
            run('spctl', '--assess', '--type', 'execute', '--verbose=2', app)
            receipt['status'] = 'accepted-and-gatekeeper-verified'
    except BaseException as error:
        receipt['failed_at'] = receipt['status']
        receipt['status'] = 'failed'
        receipt['error'] = str(error)
        raise
    finally:
        receipt_path.write_text(json.dumps(receipt, indent=2) + '\n')
        print('Notarization receipt:', receipt_path)


if __name__ == '__main__':
    main()
