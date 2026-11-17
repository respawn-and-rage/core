"""Verify a Linux snapshot carries plugin source instead of host binaries."""
import importlib.util
from importlib.machinery import SourceFileLoader
import io
import json
from pathlib import Path
import sys
import tarfile
import tempfile
import unittest
from unittest.mock import patch

spec = importlib.util.spec_from_file_location('archive_linux', Path(__file__).parents[1] / 'archive-linux-project.py')
module = importlib.util.module_from_spec(spec)
spec.loader.exec_module(module)


class LinuxPluginSnapshot(unittest.TestCase):
    def setUp(self):
        load = SourceFileLoader.exec_module

        def load_fixture(loader, loaded):
            if loaded.__name__ == 'eos_guest':
                loaded.prepare = lambda project, engine: None
            else:
                load(loader, loaded)

        patcher = patch.object(SourceFileLoader, 'exec_module', load_fixture)
        patcher.start()
        self.addCleanup(patcher.stop)

    def test_marketplace_dependency_is_rebuilt_in_disposable_project(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            project, engine = root / 'project', root / 'engine'
            project.mkdir()
            (project / 'RespawnAndRage.uproject').write_text(json.dumps({'Plugins': [{'Name': 'PCGEx', 'Enabled': True}]}))
            plugin = engine / 'Engine/Plugins/Marketplace/Plugin'
            for name in ('Source', 'Binaries/Mac', 'Intermediate/Build'):
                (plugin / name).mkdir(parents=True)
                (plugin / name / 'fixture').write_text('fixture')
            descriptor = plugin / 'PCGEx.uplugin'
            descriptor.write_text(json.dumps({'Installed': True, 'VersionName': 'fixture'}))
            for name in ('scripts/stamp-build-version.py', 'scripts/export-match-stats.py',
                         'scripts/stage-match-stats.py', 'Branding/icon.png', 'Branding/background.jpg',
                         'Branding/Credits.txt', 'Branding/Fonts/Teko/ThirdPartyLicenses/Teko-OFL.txt',
                         'Branding/Fonts/Teko/Fonts/Teko-SemiBold.ttf'):
                path = project / name
                path.parent.mkdir(parents=True, exist_ok=True)
                path.write_text('fixture')
            output = root / 'input.tar.gz'
            argv = ['archive-linux-project.py', str(output), '--project', str(project), '--engine', str(engine)]
            with patch.object(sys, 'argv', argv), patch.object(module.subprocess, 'check_output', return_value='a' * 40):
                module.main()
            with tarfile.open(output) as archive:
                names = archive.getnames()
                self.assertIn('Plugins/PCGEx/Source/fixture', names)
                self.assertIn('Branding/Fonts/Teko/Fonts/Teko-SemiBold.ttf', names)
                self.assertIn('Branding/Fonts/Teko/ThirdPartyLicenses/Teko-OFL.txt', names)
                self.assertIn('Branding/Credits.txt', names)
                self.assertIn('scripts/export-match-stats.py', names)
                self.assertIn('scripts/stage-match-stats.py', names)
                self.assertFalse(any('Binaries' in n or 'Intermediate' in n for n in names))
                self.assertFalse(json.load(archive.extractfile('Plugins/PCGEx/PCGEx.uplugin'))['Installed'])
            self.assertTrue(json.loads(descriptor.read_text())['Installed'])
            # stdout is an upload stream: diagnostic text must never corrupt gzip.
            binary = io.BytesIO()
            stdout = io.TextIOWrapper(binary, encoding='utf-8')
            with patch.object(sys, 'argv', [argv[0], '-', *argv[2:]]), patch.object(sys, 'stdout', stdout), patch.object(module.subprocess, 'check_output', return_value='a' * 40):
                module.main()
            stdout.flush()
            with tarfile.open(fileobj=io.BytesIO(binary.getvalue())) as archive:
                self.assertIn('Plugins/PCGEx/Source/fixture', archive.getnames())


if __name__ == '__main__':
    unittest.main()
