"""Exercise destructive cleanup only inside temporary fixtures."""
import importlib.util
import json
from pathlib import Path
import tempfile
import unittest

spec = importlib.util.spec_from_file_location('build_cleanup', Path(__file__).parents[1] / 'build_cleanup.py')
module = importlib.util.module_from_spec(spec)
spec.loader.exec_module(module)


class CleanupTests(unittest.TestCase):
    def fixture(self, root, name, status, release=True):
        run = root / name
        payload = run / ('build/project' if release else 'project')
        payload.mkdir(parents=True)
        (payload / 'asset').write_text('fixture')
        (run / ('release.json' if release else 'receipt.json')).write_text(json.dumps({'status': status}))
        (run / 'build.log').write_text('keep')
        return run, payload

    def test_only_completed_payloads_removed_and_receipts_retained(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            done, payload = self.fixture(root, 'done', 'uploaded')
            _, failed = self.fixture(root, 'failed', 'failed')
            _, running = self.fixture(root, 'running', 'building')
            _, ready = self.fixture(root, 'ready', 'ready')
            standalone, packaged = self.fixture(root, 'standalone', 'packaged', release=False)
            module.cleanup_old_builds([root], dry_run=True)
            self.assertTrue(payload.exists())
            module.cleanup_old_builds([root])
            self.assertFalse(payload.exists())
            self.assertFalse(packaged.exists())
            self.assertTrue(all(p.exists() for p in (failed, running, ready)))
            self.assertTrue((done / 'build.log').exists())
            self.assertIn('payload_removed_at', json.loads((done / 'release.json').read_text()))
            self.assertTrue(json.loads((standalone / 'receipt.json').read_text())['scratch_removed'])
            module.cleanup_old_builds([root])  # Idempotent.

    def test_symlinked_payload_and_run_are_preserved(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp) / 'builds'
            root.mkdir()
            outside = Path(tmp) / 'outside'
            outside.mkdir()
            (outside / 'important').write_text('keep')
            run, payload = self.fixture(root, 'done', 'uploaded')
            (payload / 'asset').unlink()
            payload.rmdir()
            payload.symlink_to(outside, target_is_directory=True)
            (root / 'linked-run').symlink_to(run, target_is_directory=True)
            module.cleanup_old_builds([root])
            self.assertTrue((outside / 'important').exists())
            self.assertTrue(payload.is_symlink())

    def test_uploaded_run_removed_completely_with_link_targets_preserved(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            project = root / 'source'
            project.mkdir()
            (project / 'important').write_text('keep')
            for receipt in ('release.json', 'summary.json', 'receipt.json'):
                run = root / receipt.replace('.json', '')
                run.mkdir()
                (run / receipt).write_text(json.dumps({'status': 'uploaded'}))
                (run / 'log').write_text('remove')
                (run / 'project').symlink_to(project, target_is_directory=True)
                module.remove_uploaded_build(run, project)
                self.assertFalse(run.exists())
                self.assertTrue((project / 'important').exists())

    def test_desktop_sweep_requires_uploaded_whole_run_summary(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            project = root / 'core'
            project.mkdir()
            output = root / 'desktop'
            output.mkdir()
            runs = {}
            for index, status in enumerate(('uploaded', 'failed', 'packaged')):
                run = output / f'20261116-17010{index}-12345678'
                run.mkdir()
                (run / 'summary.json').write_text(json.dumps({'status': status}), encoding='utf-8-sig')
                runs[status] = run
            module.cleanup_uploaded_desktop_runs(output, project)
            self.assertFalse(runs['uploaded'].exists())
            self.assertTrue(runs['failed'].exists())
            self.assertTrue(runs['packaged'].exists())

    def test_unpublished_and_source_paths_cannot_be_removed(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            project = root / 'source'
            project.mkdir()
            for status in ('ready', 'packaged', 'failed', 'uploading'):
                run, _ = self.fixture(root, status, status)
                with self.assertRaisesRegex(ValueError, 'successfully uploaded'):
                    module.remove_uploaded_build(run, project)
                self.assertTrue(run.exists())
            for path in (root, project, project / 'build'):
                path.mkdir(exist_ok=True)
                (path / 'release.json').write_text('{"status":"uploaded"}')
                with self.assertRaisesRegex(ValueError, 'outside the source'):
                    module.remove_uploaded_build(path, project)
            linked = root / 'linked'
            linked.symlink_to(project, target_is_directory=True)
            with self.assertRaisesRegex(ValueError, 'symlinked'):
                module.remove_uploaded_build(linked, project)
            self.assertTrue(project.exists())


if __name__ == '__main__':
    unittest.main()
