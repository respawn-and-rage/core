"""Regression checks for cook map coverage and packaged-runtime failure detection."""
import json
from pathlib import Path
import re
import runpy
import sys
import tempfile
from types import SimpleNamespace
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[2]
SCRIPT = ROOT / 'scripts/test-packaged-runtime.py'


class PackagedMapsTests(unittest.TestCase):
    def test_packaging_uses_config_and_config_covers_catalog(self):
        configured = set(re.findall(r'\+MapsToCook=\(FilePath="([^"]+)"\)',
                                    (ROOT / 'Config/DefaultGame.ini').read_text()))
        catalog = set(re.findall(r'TEXT\("(/Game/[^\"]+)"\)',
                                 (ROOT / 'Source/fdm/ERMapCatalog.cpp').read_text()))
        self.assertTrue(catalog)
        self.assertFalse(catalog - configured)
        for name in ('package-macos.py', 'package-windows.ps1'):
            self.assertNotRegex((ROOT / 'scripts' / name).read_text(), r"['\"]-map=")

    def run_case(self, output, listen=False, loaded=True, listening=True, failure=False):
        package = '/Game/EclipseRecoil/Maps/L_FireTraining'
        def launch(command, stdout, **kwargs):
            self.assertTrue(command[1].startswith(package + ('?listen?' if listen else '?')))
            self.assertEqual('-llm' in command, sys.platform == 'darwin')
            text = ('ER_RENDER_PROFILE aa=2 gi=0 reflections=0 shadows=1\n'
                    'ER_WEAPON_MODEL name=Pistol\n' + 'ER_BOT_JOINED\n' * 6)
            if loaded:
                text += 'LogWorld: Bringing World ' + package + '.L_FireTraining up for play\n'
            if listen and listening:
                text += 'LogNet: GameNetDriver IpNetDriver_0 listening on port 7777\n'
            if failure:
                text += 'TravelFailure: Failed to load package\n'
            stdout.write(text)
            return SimpleNamespace(wait=lambda **kw: 0)
        argv = [str(SCRIPT), '--executable', '/fixture/game', '--map-package', package,
                '--output', str(output)] + (['--listen'] if listen else [])
        with patch.object(sys, 'argv', argv), patch('subprocess.Popen', side_effect=launch):
            with self.assertRaises(SystemExit) as result:
                runpy.run_path(str(SCRIPT), run_name='__main__')
        return result.exception.code

    def test_runtime_requires_loaded_map_and_real_listen_driver(self):
        with tempfile.TemporaryDirectory() as temporary:
            output = Path(temporary)
            self.assertEqual(self.run_case(output), 0)
            self.assertEqual(self.run_case(output, listen=True), 0)
            self.assertEqual(self.run_case(output, loaded=False), 1)
            self.assertEqual(self.run_case(output, listen=True, listening=False), 1)
            self.assertEqual(self.run_case(output, failure=True), 1)

    def test_all_maps_attempts_both_modes_and_propagates_failure(self):
        with tempfile.TemporaryDirectory() as temporary:
            argv = [str(SCRIPT), '--executable', '/fixture/game', '--all-maps', '--output', temporary]
            calls = []
            def launch(command):
                calls.append(command)
                return SimpleNamespace(returncode=int('L_FireTraining' in command[-1]))
            with patch.object(sys, 'argv', argv), patch('subprocess.run', side_effect=launch):
                with self.assertRaises(SystemExit) as result:
                    runpy.run_path(str(SCRIPT), run_name='__main__')
            self.assertEqual(result.exception.code, 1)
            report = json.loads((Path(temporary) / 'maps.json').read_text())
            self.assertEqual(report['result'], 'FAIL')
            expected = set(re.findall(r'\+MapsToCook=\(FilePath="([^"]+)"\)',
                                      (ROOT / 'Config/DefaultGame.ini').read_text()))
            expected.discard('/Game/EclipseRecoil/Maps/L_MainMenu')
            self.assertEqual({(case['map'], case['listen']) for case in report['cases']},
                             {(package, listen) for package in expected for listen in (False, True)})
            self.assertTrue(all('--all-maps' not in command for command in calls))


if __name__ == '__main__':
    unittest.main()
