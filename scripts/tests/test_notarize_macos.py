"""Offline checks of the release gate; never invoke signing or Apple services."""
import importlib.util
import json
from pathlib import Path
import plistlib
import shutil
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import patch

spec = importlib.util.spec_from_file_location('notarize_macos', Path(__file__).parents[1] / 'notarize-macos.py')
module = importlib.util.module_from_spec(spec)
spec.loader.exec_module(module)
IDENTITY = 'Developer ID Application: Test Fixture (TESTTEAM)'


class NotarizationGate(unittest.TestCase):
    def exercise(self, status, identity_available=True, sign_only=False, submission_code=0):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            app = root / 'dist/Game.app'
            (app / 'Contents').mkdir(parents=True)
            (app / 'Contents/Info.plist').write_bytes(b'fixture')
            calls = []
            scratch = root / 'build-ssd'
            scratch.mkdir()

            def fake_run(*args, **kwargs):
                calls.append(tuple(map(str, args)))
                output = ''
                if args[0] == 'security':
                    output = f'1) TEST "{IDENTITY}"' if identity_available else '0 valid identities found'
                if args[:3] == ('xcrun', 'notarytool', 'submit'):
                    output = json.dumps({'id': 'fixture-id', 'status': status})
                    self.assertTrue(Path(args[3]).is_relative_to(scratch))
                    return subprocess.CompletedProcess(args, submission_code, stdout=output, stderr='')
                return subprocess.CompletedProcess(args, 0, stdout=output, stderr='')

            argv = ['notarize-macos.py', str(app), '--identity', IDENTITY, '--keychain-profile', 'fixture',
                    '--scratch-directory', str(scratch)]
            if sign_only:
                argv.append('--sign-only')
            with patch.object(module, 'run', side_effect=fake_run), patch.object(sys, 'argv', argv):
                if not identity_available:
                    with self.assertRaises(SystemExit):
                        module.main()
                elif status != 'Accepted' or submission_code:
                    with self.assertRaises(RuntimeError):
                        module.main()
                else:
                    module.main()
            receipt = root / 'Game-notarization.json'
            return calls, json.loads(receipt.read_text()) if receipt.exists() else None, list((root / 'dist').iterdir()) == [app]

    def test_missing_identity_never_changes_or_submits_app(self):
        calls, receipt, clean = self.exercise('Accepted', identity_available=False)
        self.assertEqual(len(calls), 1)
        self.assertIsNone(receipt)
        self.assertTrue(clean)

    def test_rejected_submission_never_reports_ready_or_staples(self):
        calls, receipt, clean = self.exercise('Invalid')
        self.assertEqual(receipt['status'], 'failed')
        self.assertIn('Apple did not accept', receipt['error'])
        self.assertFalse(any(c[:2] == ('xcrun', 'stapler') or c[0] == 'spctl' for c in calls))
        self.assertTrue(clean)

    def test_accepted_submission_requires_gatekeeper_check(self):
        calls, receipt, clean = self.exercise('Accepted')
        self.assertEqual(receipt['status'], 'accepted-and-gatekeeper-verified')
        self.assertEqual(calls[-1][0], 'spctl')
        self.assertTrue(clean)

    def test_sign_only_does_not_claim_notarization(self):
        calls, receipt, clean = self.exercise('Accepted', sign_only=True)
        self.assertEqual(receipt['status'], 'signed-not-yet-notarized')
        self.assertFalse(any(c[:2] == ('xcrun', 'notarytool') for c in calls))
        self.assertTrue(clean)

    def test_failed_submission_command_never_staples_even_with_accepted_json(self):
        calls, receipt, clean = self.exercise('Accepted', submission_code=69)
        self.assertEqual(receipt['status'], 'failed')
        self.assertFalse(any(c[:2] == ('xcrun', 'stapler') for c in calls))
        self.assertTrue(clean)

    def test_automatic_identity_selection_refuses_ambiguity(self):
        second = 'Developer ID Application: Other (OTHERTEAM)'
        with patch.object(module, 'run', return_value=subprocess.CompletedProcess([], 0,
                          stdout=f'1) TEST "{IDENTITY}"\n2) TEST "{second}"')) as run:
            with self.assertRaisesRegex(RuntimeError, 'several'):
                module.check_credentials(None, 'fixture')
            self.assertEqual(run.call_count, 1)
        with patch.object(module, 'run', return_value=subprocess.CompletedProcess([], 0,
                          stdout=f'1) TEST "{IDENTITY}"')) as run:
            self.assertEqual(module.check_credentials(None, 'fixture'), IDENTITY)
            self.assertEqual(run.call_args.args[:3], ('xcrun', 'notarytool', 'history'))

    def test_auto_notarization_uses_existing_xcode_account_when_profile_is_missing(self):
        def fake(*args, **kwargs):
            if args[0] == 'security':
                return subprocess.CompletedProcess(args, 0, stdout=f'1) TEST "{IDENTITY}"')
            if args[:2] == ('xcrun', 'notarytool'):
                return subprocess.CompletedProcess(args, 69, stdout='', stderr='profile missing')
            if args[0] == 'defaults':
                return subprocess.CompletedProcess(args, 0, stdout='public account metadata')
            if args[0] == 'plutil':
                return subprocess.CompletedProcess(args, 0, stdout=json.dumps({'IDE.Prod': ['fixture account']}))
            raise AssertionError(args)
        with patch.object(module, 'run', side_effect=fake):
            self.assertEqual(module.resolve_credentials(None, 'missing'), (IDENTITY, 'xcode'))
            with self.assertRaisesRegex(RuntimeError, 'profile unavailable'):
                module.resolve_credentials(None, 'missing', 'notarytool')

    def test_empty_xcode_account_lists_are_not_a_configured_account(self):
        with patch.object(module, 'run', side_effect=[subprocess.CompletedProcess([], 0, stdout='{}'),
                          subprocess.CompletedProcess([], 0, stdout='{"IDE.Prod": []}')]):
            self.assertFalse(module.xcode_account_available())

    def test_xcode_check_reports_configuration_without_claiming_authentication(self):
        with tempfile.TemporaryDirectory() as temporary:
            app = Path(temporary) / 'Game.app'
            (app / 'Contents').mkdir(parents=True)
            (app / 'Contents/Info.plist').write_bytes(b'fixture')
            argv = ['notarize-macos.py', str(app), '--identity', IDENTITY, '--method', 'xcode', '--check']
            with patch.object(sys, 'argv', argv), patch.object(module, 'resolve_credentials'), \
                    patch('builtins.print') as output, patch.object(module, 'run') as run:
                module.main()
            run.assert_not_called()
            self.assertIn('authentication is checked only during upload', output.call_args.args[0])
            self.assertFalse((app.parent / 'Game-notarization.json').exists())


class XcodeNotarizationGate(unittest.TestCase):
    def exercise(self, failure=None, pending=False):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            app = root / 'dist/Game.app'
            version = Path('Contents/UE/RespawnAndRage/Build/GameVersion.txt')
            (app / version).parent.mkdir(parents=True)
            (app / version).write_text('26.10.20.1234abcd')
            (app / 'Contents/MacOS').mkdir()
            (app / 'Contents/MacOS/Game').write_text('original')
            (app / 'Contents/Info.plist').write_bytes(plistlib.dumps({
                'CFBundleIdentifier': 'org.fixture.Game', 'CFBundleExecutable': 'Game', 'CFBundleVersion': '1'}))
            scratch = root / 'scratch'
            scratch.mkdir()
            calls = []
            exports = []
            distribution_temporaries = []
            receipt = {}

            def fake(*args, **kwargs):
                command = list(map(str, args))
                calls.append(command)
                if command[0] == 'xcodebuild':
                    temporary = Path(kwargs['env']['TMPDIR'])
                    self.assertEqual(temporary.parent, scratch)
                    self.assertTrue(temporary.is_dir())
                    (temporary / 'staged-app.zip').touch()
                    distribution_temporaries.append(temporary)
                if command[0] == 'lipo':
                    return subprocess.CompletedProcess(command, 0, stdout='arm64 x86_64')
                if '-exportArchive' in command:
                    options = plistlib.loads(Path(command[command.index('-exportOptionsPlist') + 1]).read_bytes())
                    self.assertEqual(options['method'], 'developer-id')
                    self.assertEqual(options['destination'], 'upload')
                    self.assertNotIn('password', str(options).lower())
                    if failure == 'upload':
                        raise subprocess.CalledProcessError(1, command)
                    if failure in ('auth-upload', 'upload-exit'):
                        diagnostic = ('Invalid credentials in keychain, missing Xcode-Token; error: exportArchive No Accounts'
                                      if failure == 'auth-upload' else 'Network unavailable')
                        return subprocess.CompletedProcess(command, 70, stdout='', stderr=diagnostic)
                if '-exportNotarizedApp' in command:
                    exports.append(command)
                    if failure == 'auth-export':
                        return subprocess.CompletedProcess(command, 70, stdout='', stderr='No Accounts: missing Xcode-Token')
                    if pending and len(exports) == 1:
                        return subprocess.CompletedProcess(command, 1, stdout='Archive has not yet been notarized', stderr='')
                    final = Path(command[command.index('-exportPath') + 1]) / app.name
                    shutil.copytree(app, final)
                    (final / 'Contents/MacOS/Game').write_text('notarized export')
                    if failure == 'version':
                        (final / version).write_text('26.10.20.deadbeef')
                if failure == 'ticket' and command[:3] == ['xcrun', 'stapler', 'validate']:
                    raise subprocess.CalledProcessError(65, command)
                return subprocess.CompletedProcess(command, 0, stdout='', stderr='')

            with patch.object(module, 'run', side_effect=fake), \
                    patch.object(module, 'copy_app', side_effect=lambda src, dst: shutil.copytree(src, dst)), \
                    patch.object(module.time, 'sleep') as sleep:
                identity = 'Developer ID Application: Fixture (ABCDEFGHIJ)'
                if failure:
                    with self.assertRaises((RuntimeError, subprocess.CalledProcessError)) as caught:
                        module.notarize_with_xcode(app, identity, scratch, receipt, 60)
                    self.assertEqual((app / 'Contents/MacOS/Game').read_text(), 'original')
                    if failure.startswith('auth-'):
                        self.assertIn('Xcode > Settings > Accounts', str(caught.exception))
                        self.assertIn('--resume', str(caught.exception))
                        self.assertTrue(Path(receipt['submission']['archive']).is_dir())
                        sleep.assert_not_called()
                        self.assertFalse(any(c[0] in ('codesign', 'spctl') or c[:2] == ['xcrun', 'stapler'] for c in calls))
                    if failure == 'upload-exit':
                        self.assertIn('Network unavailable', str(caught.exception))
                        self.assertNotIn('sign in again', str(caught.exception))
                        self.assertFalse(exports)
                else:
                    module.notarize_with_xcode(app, identity, scratch, receipt, 60)
                    self.assertEqual((app / 'Contents/MacOS/Game').read_text(), 'notarized export')
                    self.assertEqual(receipt['submission']['status'], 'exported-notarized-app')
                    self.assertTrue(Path(receipt['submission']['archive']).is_dir())
                self.assertTrue(distribution_temporaries)
                self.assertTrue(all(not p.exists() for p in distribution_temporaries))
                if pending:
                    sleep.assert_called_once_with(15)
                return calls

    def test_xcode_export_requires_ticket_signature_and_gatekeeper(self):
        calls = self.exercise()
        self.assertTrue(any(c[:3] == ['xcrun', 'stapler', 'validate'] for c in calls))
        self.assertTrue(any(c[0] == 'spctl' for c in calls))

    def test_pending_notarization_is_polled_before_export(self):
        self.exercise(pending=True)

    def test_xcode_failures_preserve_the_original_app(self):
        for failure in ('upload', 'ticket', 'version'):
            self.exercise(failure=failure)

    def test_missing_xcode_token_reports_recovery_and_never_polls_or_validates(self):
        for failure in ('auth-upload', 'auth-export'):
            with self.subTest(failure=failure):
                self.exercise(failure=failure)

    def test_nonzero_upload_exit_stops_before_export(self):
        self.exercise(failure='upload-exit')


if __name__ == '__main__':
    unittest.main()
