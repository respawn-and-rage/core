import importlib.util
import json
from pathlib import Path
import tempfile
import unittest
import runpy
from types import SimpleNamespace
from unittest.mock import Mock

spec = importlib.util.spec_from_file_location('asset_setup', Path(__file__).resolve().parents[1] / 'setup-assets.py')
module = importlib.util.module_from_spec(spec)
spec.loader.exec_module(module)


class AssetSetupTests(unittest.TestCase):
    def test_python39_junction_fallback_checks_exact_reparse_tag(self):
        # Simulate the Python 3.9 Path API, which has no is_junction method.
        path = Mock(spec=['is_symlink', 'lstat'])
        path.is_symlink.return_value = False
        path.lstat.return_value = SimpleNamespace(st_reparse_tag=0xA0000003)
        self.assertTrue(module.is_link(path))
        for attributes in ({}, {'st_reparse_tag': 0x8000001B}):
            path.lstat.return_value = SimpleNamespace(**attributes)
            self.assertFalse(module.is_link(path))
        path.lstat.side_effect = FileNotFoundError
        self.assertFalse(module.is_link(path))

    def fixture(self, root):
        (root / 'assets.json').write_text(json.dumps({'repositories': {
            'characters': {'path': 'Assets/characters', 'mounts': ['Content/Characters']},
            'maps': {'path': 'Assets/maps', 'mounts': ['Content/EclipseRecoil/Maps']}}}))
        for folder in ('Assets/characters/Content/Characters', 'Assets/maps/Content/EclipseRecoil/Maps'):
            (root / folder).mkdir(parents=True)
            (root / folder / 'test.uasset').write_bytes(b'asset data')

    def test_preserves_bytes_and_relative_paths_and_is_idempotent(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            self.fixture(root)
            self.assertEqual(module.mount(root), 2)
            self.assertEqual(module.mount(root), 2)
            self.assertEqual((root / 'Content/Characters/test.uasset').read_bytes(), b'asset data')
            self.assertTrue((root / 'Content/Characters').is_symlink())
            self.assertFalse((root / 'Content/Characters').readlink().is_absolute())
            module.validate(root, require_mounts=True)
            iterator = runpy.run_path(str(Path(__file__).resolve().parents[1] / 'asset_files.py'))['content_files']
            self.assertEqual({p.relative_to(root).as_posix() for p in iterator(root)},
                             {'Content/Characters/test.uasset', 'Content/EclipseRecoil/Maps/test.uasset'})

    def test_existing_content_is_never_overwritten_and_no_partial_mounts(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            self.fixture(root)
            (root / 'Content/EclipseRecoil/Maps').mkdir(parents=True)
            marker = root / 'Content/EclipseRecoil/Maps/edited.umap'
            marker.write_bytes(b'edited')
            with self.assertRaisesRegex(RuntimeError, 'replace existing data'):
                module.mount(root)
            self.assertEqual(marker.read_bytes(), b'edited')
            self.assertFalse((root / 'Content/Characters').exists())

    def test_lfs_pointer_blocks_entire_plan(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            self.fixture(root)
            (root / 'Assets/maps/Content/EclipseRecoil/Maps/test.uasset').write_bytes(module.LFS_HEADER + b'\n')
            with self.assertRaisesRegex(RuntimeError, 'Unresolved LFS'):
                module.mount(root)
            self.assertFalse((root / 'Content/Characters').exists())

    def test_archived_sources_are_optional_and_mount_after_restore(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            self.fixture(root)
            manifest = root / 'assets.json'
            data = json.loads(manifest.read_text())
            data['repositories']['maps']['mounts'].append('SourceAssets/Textures')
            manifest.write_text(json.dumps(data))
            self.assertEqual(module.mount(root), 2)
            self.assertEqual(len(module.validate(root, require_mounts=True)), 2)

            source = root / 'Assets/maps/SourceAssets/Textures'
            source.mkdir(parents=True)
            (source / 'original.png').write_bytes(b'original texture')
            self.assertEqual(module.mount(root), 3)
            self.assertEqual((root / 'SourceAssets/Textures/original.png').read_bytes(), b'original texture')

            (source / 'original.png').unlink()
            source.rmdir()
            self.assertEqual(len(module.validate(root, require_mounts=True)), 2)

    def test_conflicting_symlink_is_preserved(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            self.fixture(root)
            (root / 'Content').mkdir()
            target = root / 'Assets/maps/Content/EclipseRecoil/Maps'
            (root / 'Content/Characters').symlink_to(target)
            with self.assertRaisesRegex(RuntimeError, 'Conflicting mount'):
                module.mount(root)
            self.assertEqual((root / 'Content/Characters').resolve(), target.resolve())

    def test_invalid_or_overlapping_paths_are_rejected(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            for mounts in (['../secret'], ['Content', 'Content/Characters']):
                (root / 'assets.json').write_text(json.dumps({'repositories': {
                    'characters': {'path': 'Assets/characters', 'mounts': mounts}}}))
                with self.assertRaises(RuntimeError):
                    module.layout(root)


if __name__ == '__main__':
    unittest.main()
