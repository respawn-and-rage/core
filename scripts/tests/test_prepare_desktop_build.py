import importlib.util
from importlib.machinery import SourceFileLoader
import json
import os
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch

spec = importlib.util.spec_from_file_location('desktop_workspace', Path(__file__).parents[1] / 'prepare-desktop-build.py')
module = importlib.util.module_from_spec(spec)
spec.loader.exec_module(module)


class DesktopWorkspaceTests(unittest.TestCase):
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

    def fixture(self, root):
        project, engine = root / 'core', root / 'engine'
        for name in ('RespawnAndRage.uproject', 'Source/game.cpp', 'Config/DefaultGame.ini',
                     'Build/GameVersion.txt', 'scripts/stamp-build-version.py', 'scripts/export-match-stats.py',
                     'Content/map.uasset',
                     'Branding/icon.png', 'Branding/background.jpg', 'Branding/Credits.txt',
                     'Branding/Fonts/Teko/Fonts/Teko-SemiBold.ttf'):
            path = project / name
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_text('fixture')
        (project / 'RespawnAndRage.uproject').write_text(json.dumps({'Plugins': [{'Name': 'PCGEx', 'Enabled': True}]}))
        plugin = engine / 'Engine/Plugins/Marketplace/PCGEx'
        for name in ('Source/game.cpp', 'Binaries/Win64/fixture.dll', 'Intermediate/fixture'):
            path = plugin / name
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_text('fixture')
        (plugin / 'PCGEx.uplugin').write_text(json.dumps({'Installed': True, 'Modules': [{'Name': 'PCGEx'}]}))
        (project / '.git').mkdir()
        return project, engine, plugin

    @unittest.skipIf(os.name == 'nt', 'Shared Content junction creation needs a Windows integration run')
    def test_workspace_rebuilds_plugins_without_touching_engine_or_source(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            project, engine, original_plugin = self.fixture(root)
            workspace = root / 'builds with spaces/project'
            with patch.object(module.subprocess, 'check_output', side_effect=['', '2026-10-20T23:30:00-07:00', 'a' * 40]):
                result = module.prepare(project, engine, workspace)
            self.assertEqual(result, workspace.resolve() / 'RespawnAndRage.uproject')
            self.assertEqual((workspace / 'Content/map.uasset').read_text(), 'fixture')
            self.assertTrue((workspace / 'Content').is_symlink())
            self.assertEqual((workspace / 'Build/SourceCommit.txt').read_text().strip(), 'a' * 40)
            self.assertFalse((workspace / 'Build/GameVersion.txt').exists())
            self.assertFalse((workspace / 'Plugins/PCGEx/Binaries').exists())
            self.assertFalse((workspace / 'Plugins/PCGEx/Intermediate').exists())
            self.assertFalse(json.loads((workspace / 'Plugins/PCGEx/PCGEx.uplugin').read_text())['Installed'])
            self.assertTrue(json.loads((original_plugin / 'PCGEx.uplugin').read_text())['Installed'])
            (workspace / 'Source/game.cpp').write_text('build-only change')
            self.assertEqual((project / 'Source/game.cpp').read_text(), 'fixture')
            self.assertTrue((workspace / 'Branding/Fonts/Teko/Fonts/Teko-SemiBold.ttf').is_file())
            self.assertTrue((workspace / 'scripts/stamp-build-version.py').is_file())
            self.assertTrue((workspace / 'scripts/export-match-stats.py').is_file())

    def test_existing_or_nested_workspace_is_rejected_before_writes(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            project, engine, _ = self.fixture(root)
            with self.assertRaisesRegex(ValueError, 'outside'):
                module.prepare(project, engine, project / 'build')
            workspace = root / 'existing'
            workspace.mkdir()
            (workspace / 'keep.txt').write_text('keep')
            with self.assertRaisesRegex(ValueError, 'already exist'):
                module.prepare(project, engine, workspace)
            self.assertEqual((workspace / 'keep.txt').read_text(), 'keep')


if __name__ == '__main__':
    unittest.main()
