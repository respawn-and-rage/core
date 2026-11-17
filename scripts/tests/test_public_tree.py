import importlib.util
import hashlib
from pathlib import Path
import subprocess
import tempfile
import unittest
from unittest.mock import patch

spec = importlib.util.spec_from_file_location('public_tree', Path(__file__).resolve().parents[1] / 'verify-public-tree.py')
module = importlib.util.module_from_spec(spec)
spec.loader.exec_module(module)


class PublicTreeTests(unittest.TestCase):
    def git(self, root, *args):
        return subprocess.check_output(['git', *args], cwd=root, stderr=subprocess.STDOUT)

    def fixture(self, root):
        self.git(root, 'init', '-b', 'main')
        self.git(root, 'config', 'user.name', 'Test')
        self.git(root, 'config', 'user.email', 'test@example.invalid')
        # Avoid inherited LFS filters: the public parent owns only text.
        (root / '.gitattributes').write_text('* text=auto\n')
        (root / 'README.md').write_text('Public code\n')
        self.git(root, 'add', '.')
        self.git(root, 'commit', '-m', 'Code')

    def test_text_and_gitlink_are_allowed(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            self.fixture(root)
            oid = self.git(root, 'rev-parse', 'HEAD').decode().strip()
            self.git(root, 'update-index', '--add', '--cacheinfo', '160000', oid, 'Assets/characters')
            self.git(root, 'commit', '-m', 'Asset link only')
            self.assertEqual(module.verify(root)[0], 2)

    def test_exact_eos_guest_snippets_are_allowed(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            self.fixture(root)
            for name in module.PUBLIC_EOS_GUEST_TEXT:
                target = root / name
                target.parent.mkdir(parents=True, exist_ok=True)
                target.write_bytes((module.ROOT / name).read_bytes())
            self.git(root, 'add', '.')
            self.git(root, 'commit', '-m', 'Owned guest snippets')
            self.assertEqual(module.verify(root)[0], 2)

    def test_eos_guest_exception_remains_narrow_and_text_only(self):
        cases = [(name, content, error)
                 for name in module.PUBLIC_EOS_GUEST_TEXT
                 for content, error in ((b'private\0asset', 'binary/LFS'),
                                        (b'version https://git-lfs.github.com/spec/v1\n', 'binary/LFS'),
                                        (None, 'forbidden path/mode'))]
        cases += [(name, b'// Unapproved source\n', 'forbidden path/mode')
                  for name in ('scripts/eos-guest/Extra.inl', 'scripts/eos-guest/Extra.h',
                               'scripts/other/RRGuestEOS.h',
                               'Plugins/OnlineSubsystemEOS/Source/RRGuestEOS.h')]
        for name, content, error in cases:
            with self.subTest(name=name, content=content), tempfile.TemporaryDirectory() as directory:
                root = Path(directory)
                self.fixture(root)
                target = root / name
                target.parent.mkdir(parents=True, exist_ok=True)
                if content is None:
                    target.symlink_to('../../README.md')
                else:
                    target.write_bytes(content)
                self.git(root, 'add', '.')
                self.git(root, 'commit', '-m', 'Invalid guest snippet')
                with self.assertRaisesRegex(RuntimeError, error):
                    module.verify(root)

    def test_exact_gameplay_catalog_and_readme_are_allowed(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            self.fixture(root)
            for name in module.PUBLIC_GAMEPLAY_TEXT:
                target = root / name
                target.parent.mkdir(parents=True, exist_ok=True)
                target.write_bytes((module.ROOT / name).read_bytes())
            self.git(root, 'add', '.')
            self.git(root, 'commit', '-m', 'Gameplay message copy')
            self.assertEqual(module.verify(root)[0], 2)

    def test_gameplay_copy_still_rejects_binary_lfs_and_symlinks(self):
        for name in module.PUBLIC_GAMEPLAY_TEXT:
            for kind, content, error in (
                    ('binary', b'private\0asset', 'binary/LFS'),
                    ('lfs', b'version https://git-lfs.github.com/spec/v1\n', 'binary/LFS'),
                    ('symlink', None, 'forbidden path/mode')):
                with self.subTest(name=name, kind=kind), tempfile.TemporaryDirectory() as directory:
                    root = Path(directory)
                    self.fixture(root)
                    target = root / name
                    target.parent.mkdir(parents=True)
                    if kind == 'symlink':
                        target.symlink_to('../../README.md')
                    else:
                        target.write_bytes(content)
                    self.git(root, 'add', '.')
                    self.git(root, 'commit', '-m', 'Invalid gameplay copy')
                    with self.assertRaisesRegex(RuntimeError, error):
                        module.verify(root)

    def test_gameplay_exception_does_not_allow_other_config_paths(self):
        for name in ('Config/GameplayMessages/extra.json',
                     'Config/Other/respawn-and-rage-gameplay-messages.en-US.json',
                     'Config/GameplayMessages/extra.md',
                     'Config/GameplayMessages/private.uasset'):
            with self.subTest(name=name), tempfile.TemporaryDirectory() as directory:
                root = Path(directory)
                self.fixture(root)
                target = root / name
                target.parent.mkdir(parents=True)
                target.write_text('Unapproved content\n')
                self.git(root, 'add', '.')
                self.git(root, 'commit', '-m', 'Unapproved config path')
                with self.assertRaisesRegex(RuntimeError, 'forbidden path/mode'):
                    module.verify(root)

    def test_server_chat_template_exception_is_exact_and_text_only(self):
        template = 'scripts/server-chat/stack.yaml'
        cases = [
            (template, (module.ROOT / template).read_bytes(), None),
            (template, b'private\0asset', 'binary/LFS'),
            (template, b'version https://git-lfs.github.com/spec/v1\n', 'binary/LFS'),
            ('scripts/server-chat/deployment.yaml', b'Private: configuration\n', 'forbidden path/mode'),
        ]
        for name, content, error in cases:
            with self.subTest(name=name, error=error), tempfile.TemporaryDirectory() as directory:
                root = Path(directory)
                self.fixture(root)
                target = root / name
                target.parent.mkdir(parents=True)
                target.write_bytes(content)
                self.git(root, 'add', '.')
                self.git(root, 'commit', '-m', 'Add deployment file')
                if error:
                    with self.assertRaisesRegex(RuntimeError, error):
                        module.verify(root)
                else:
                    self.assertEqual(module.verify(root)[0], 2)

    def test_powershell_source_allowed_but_binary_in_script_rejected(self):
        for content, allowed in ((b'Write-Host "Windows build"\n', True), (b'binary\0payload', False)):
            with self.subTest(allowed=allowed), tempfile.TemporaryDirectory() as directory:
                root = Path(directory)
                self.fixture(root)
                (root / 'scripts').mkdir()
                (root / 'scripts/package-windows.ps1').write_bytes(content)
                self.git(root, 'add', '.')
                self.git(root, 'commit', '-m', 'Windows helper')
                if allowed:
                    self.assertEqual(module.verify(root)[0], 2)
                else:
                    with self.assertRaisesRegex(RuntimeError, 'binary/LFS'):
                        module.verify(root)

    def test_removed_asset_still_blocks_publication_of_history(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            self.fixture(root)
            (root / 'Content').mkdir()
            (root / 'Content/private.uasset').write_bytes(b'private data')
            self.git(root, 'add', '.')
            self.git(root, 'commit', '-m', 'Asset leak')
            self.git(root, 'rm', 'Content/private.uasset')
            self.git(root, 'commit', '-m', 'Remove from current tree')
            with self.assertRaisesRegex(RuntimeError, 'forbidden path'):
                module.verify(root)

    def test_shared_blobs_are_read_once_across_paths_and_commits(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            self.fixture(root)
            (root / 'docs').mkdir()
            (root / 'docs/copy.md').write_bytes((root / 'README.md').read_bytes())
            self.git(root, 'add', '.')
            self.git(root, 'commit', '-m', 'Same content under another allowed path')
            self.git(root, 'commit', '--allow-empty', '-m', 'Unchanged tree')
            with patch.object(module, 'read_blob_facts', wraps=module.read_blob_facts) as reader, \
                 patch.object(module.subprocess, 'Popen', wraps=subprocess.Popen) as processes:
                self.assertEqual(module.verify(root), (3, 8))
            self.assertEqual(reader.call_count, 2)
            batches = [call for call in processes.call_args_list
                       if call.args[0] == ['git', 'cat-file', '--batch']]
            self.assertEqual(len(batches), 1)

    def test_cached_content_does_not_approve_a_forbidden_path(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            self.fixture(root)
            (root / 'Content').mkdir()
            (root / 'Content/private.uasset').write_bytes((root / 'README.md').read_bytes())
            self.git(root, 'add', '.')
            self.git(root, 'commit', '-m', 'Same blob at forbidden path')
            with self.assertRaisesRegex(RuntimeError, 'forbidden path/mode Content/private.uasset'):
                module.verify(root)

    def test_asset_on_another_branch_still_blocks_publication(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            self.fixture(root)
            self.git(root, 'checkout', '-b', 'private-branch')
            (root / 'Content').mkdir()
            (root / 'Content/private.uasset').write_bytes(b'private asset')
            self.git(root, 'add', '.')
            self.git(root, 'commit', '-m', 'Asset on another branch')
            self.git(root, 'checkout', 'main')
            with self.assertRaisesRegex(RuntimeError, 'forbidden path/mode Content/private.uasset'):
                module.verify(root)

    def test_cached_branding_hash_is_checked_against_each_path(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            self.fixture(root)
            content = b'approved for one path only'
            for name in ('Branding/icon.png', 'Branding/logo.png'):
                target = root / name
                target.parent.mkdir(exist_ok=True)
                target.write_bytes(content)
            self.git(root, 'add', '.')
            self.git(root, 'commit', '-m', 'Same content under two branding paths')
            with patch.dict(module.PUBLIC_BRANDING, {
                    'Branding/icon.png': hashlib.sha256(content).hexdigest(),
                    'Branding/logo.png': hashlib.sha256(b'different approved logo').hexdigest()}):
                with self.assertRaisesRegex(RuntimeError, 'unapproved branding content Branding/logo.png'):
                    module.verify(root)

    def test_branding_approval_does_not_approve_same_blob_as_source(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            self.fixture(root)
            name = 'Branding/icon.png'
            content = b'approved\0image'
            for path in (name, 'Source/disguised.cpp'):
                target = root / path
                target.parent.mkdir(parents=True)
                target.write_bytes(content)
            self.git(root, 'add', '.')
            self.git(root, 'commit', '-m', 'Approved branding reused as source')
            with patch.dict(module.PUBLIC_BRANDING, {name: hashlib.sha256(content).hexdigest()}):
                with self.assertRaisesRegex(RuntimeError, 'binary/LFS blob Source/disguised.cpp'):
                    module.verify(root)

    def test_batch_reads_empty_and_large_blobs_without_losing_framing(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            self.fixture(root)
            (root / 'docs').mkdir()
            (root / 'docs/empty.md').write_bytes(b'')
            target = root / 'docs/large.md'
            target.write_bytes(b'a' * (1024 * 1024) + b'\0tail')
            self.git(root, 'add', '.')
            self.git(root, 'commit', '-m', 'Large binary disguised as text')
            with self.assertRaisesRegex(RuntimeError, 'binary/LFS blob docs/large.md'):
                module.verify(root)

    def test_binary_blob_disguised_as_source_is_rejected(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            self.fixture(root)
            (root / 'Source').mkdir()
            (root / 'Source/disguised.cpp').write_bytes(b'binary\0data')
            self.git(root, 'add', '.')
            self.git(root, 'commit', '-m', 'Invalid source')
            with self.assertRaisesRegex(RuntimeError, 'binary/LFS'):
                module.verify(root)

    def test_exact_branding_and_objective_c_source_are_allowed(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            self.fixture(root)
            name = 'Content/Splash/Icon.png'
            target = root / name
            target.parent.mkdir(parents=True)
            target.write_bytes((module.ROOT / name).read_bytes())
            source = root / 'Source/fdm/Mac/ERBrandingMac.mm'
            source.parent.mkdir(parents=True)
            source.write_text('// Native application icon\n')
            self.git(root, 'add', '.')
            self.git(root, 'commit', '-m', 'Approved branding')
            self.assertEqual(module.verify(root)[0], 2)

    def test_branding_path_does_not_allow_different_bytes_or_lfs(self):
        for content in (b'private\0asset', b'version https://git-lfs.github.com/spec/v1\n'):
            with self.subTest(content=content), tempfile.TemporaryDirectory() as directory:
                root = Path(directory)
                self.fixture(root)
                target = root / 'Branding/icon.png'
                target.parent.mkdir()
                target.write_bytes(content)
                self.git(root, 'add', '.')
                self.git(root, 'commit', '-m', 'Substituted branding')
                with self.assertRaisesRegex(RuntimeError, 'unapproved branding content'):
                    module.verify(root)

    def test_approved_menu_fonts_and_companion_text_are_allowed(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            self.fixture(root)
            fonts = {name for name in module.PUBLIC_BRANDING if name.endswith('.ttf')}
            self.assertEqual(len(fonts), 3)
            for name in fonts | module.PUBLIC_BRANDING_TEXT:
                target = root / name
                target.parent.mkdir(parents=True, exist_ok=True)
                target.write_bytes((module.ROOT / name).read_bytes())
            self.git(root, 'add', '.')
            self.git(root, 'commit', '-m', 'Approved fonts and credits')
            self.assertEqual(module.verify(root)[0], 2)

    def test_font_exception_rejects_replacement_or_additional_binary(self):
        for name, message in (('Branding/Fonts/Teko/Fonts/Teko-SemiBold.ttf', 'unapproved branding content'),
                              ('Branding/Fonts/Other.ttf', 'forbidden path')):
            with self.subTest(name=name), tempfile.TemporaryDirectory() as directory:
                root = Path(directory)
                self.fixture(root)
                target = root / name
                target.parent.mkdir(parents=True)
                target.write_bytes(b'unapproved\0font')
                self.git(root, 'add', '.')
                self.git(root, 'commit', '-m', 'Unapproved font')
                with self.assertRaisesRegex(RuntimeError, message):
                    module.verify(root)

    def test_approved_branding_text_still_rejects_binary_and_lfs(self):
        for content in (b'private\0data', b'version https://git-lfs.github.com/spec/v1\n'):
            with self.subTest(content=content), tempfile.TemporaryDirectory() as directory:
                root = Path(directory)
                self.fixture(root)
                target = root / 'Branding/Credits.txt'
                target.parent.mkdir()
                target.write_bytes(content)
                self.git(root, 'add', '.')
                self.git(root, 'commit', '-m', 'Invalid credits')
                with self.assertRaisesRegex(RuntimeError, 'binary/LFS'):
                    module.verify(root)

    def test_reviewed_plist_history_remains_allowed_but_third_variant_fails(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            self.fixture(root)
            name = 'Build/Mac/Resources/Info.Template.plist'
            target = root / name
            target.parent.mkdir(parents=True)
            old, current = b'<plist>old</plist>\n', b'<plist>current</plist>\n'
            with patch.dict(module.PUBLIC_BRANDING, {name: hashlib.sha256(current).hexdigest()}), \
                 patch.dict(module.PUBLIC_BRANDING_HISTORY, {name: {hashlib.sha256(old).hexdigest()}}):
                for contents in (old, current):
                    target.write_bytes(contents)
                    self.git(root, 'add', '.')
                    self.git(root, 'commit', '-m', 'Reviewed plist version')
                self.assertEqual(module.verify(root)[0], 3)
                target.write_bytes(b'<plist>unreviewed</plist>\n')
                self.git(root, 'add', '.')
                self.git(root, 'commit', '-m', 'Unreviewed plist version')
                with self.assertRaisesRegex(RuntimeError, 'unapproved branding content'):
                    module.verify(root)

    def test_other_images_and_branding_symlinks_remain_forbidden(self):
        for name, symlink in (('Branding/private.png', False), ('Branding/icon.png', True)):
            with self.subTest(name=name), tempfile.TemporaryDirectory() as directory:
                root = Path(directory)
                self.fixture(root)
                target = root / name
                target.parent.mkdir()
                if symlink:
                    target.symlink_to('../README.md')
                else:
                    target.write_bytes(b'private\0image')
                self.git(root, 'add', '.')
                self.git(root, 'commit', '-m', 'Unapproved asset')
                with self.assertRaisesRegex(RuntimeError, 'forbidden path'):
                    module.verify(root)


if __name__ == '__main__':
    unittest.main()
