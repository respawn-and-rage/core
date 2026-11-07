"""Tooling contracts only: fixtures below are not cooked or runnable packages."""
import contextlib
import importlib.util
import io
import json
import os
from pathlib import Path
import struct
import tempfile
import unittest
from unittest.mock import patch

spec = importlib.util.spec_from_file_location('build_server', Path(__file__).parents[1] / 'build-server-linux.py')
build = importlib.util.module_from_spec(spec)
spec.loader.exec_module(build)
launch = build.helper('run-server-linux.py')


def write(path, contents):
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(contents)
    return path


def elf_fixture(path, machine=62):
    # A structural ELF fixture with one executable segment; never execute it.
    header = bytearray(128)
    header[:7] = b'\x7fELF\x02\x01\x01'
    struct.pack_into('<HHIQQ', header, 16, 3, machine, 1, 120, 64)
    struct.pack_into('<HHH', header, 52, 64, 56, 1)
    struct.pack_into('<IIQQQQQQ', header, 64, 1, 5, 0, 0, 0, 128, 128, 4096)
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_bytes(header)
    path.chmod(0o755)
    return path


def engine_fixture(root, server=True, required_file=''):
    engine = root / 'engine with spaces'
    write(engine / 'Engine/Build/Build.version', json.dumps({'MajorVersion': 5, 'MinorVersion': 8, 'PatchVersion': 3}))
    write(engine / 'Engine/Build/InstalledBuild.txt', '')
    declarations = '[InstalledPlatforms]\nHasInstalledPlatformInfo=true\n'
    for target in ('Editor', 'Server') if server else ('Editor', 'Game'):
        declarations += ('+InstalledPlatformConfigurations=(PlatformName="Linux", Configuration="Development", '
                         f'PlatformType="{target}", Architecture="x64", RequiredFile="{required_file}")\n')
    write(engine / 'Engine/Config/BaseEngine.ini', declarations)
    for filename in ('Engine/Build/BatchFiles/Linux/Build.sh', 'Engine/Build/BatchFiles/RunUAT.sh',
                     'Engine/Binaries/Linux/UnrealEditor-Cmd'):
        write(engine / filename, 'fixture')
    write(engine / 'Engine/Binaries/ThirdParty/DotNet/10.0/linux-x64/dotnet', 'fixture').chmod(0o755)
    return engine


class ServerBuildTests(unittest.TestCase):
    def test_installed_game_support_does_not_imply_server_support(self):
        with tempfile.TemporaryDirectory() as temporary:
            engine = engine_fixture(Path(temporary), server=False)
            before = {p: p.read_bytes() for p in engine.rglob('*') if p.is_file()}
            with self.assertRaisesRegex(ValueError, 'Development Server support'):
                build.check_engine(engine)
            self.assertEqual(before, {p: p.read_bytes() for p in engine.rglob('*') if p.is_file()})

    def test_installed_required_files_and_version_are_enforced(self):
        with tempfile.TemporaryDirectory() as temporary:
            engine = engine_fixture(Path(temporary), required_file='Engine/Binaries/Linux/UnrealServer.target')
            with self.assertRaisesRegex(ValueError, 'downloaded'):
                build.check_engine(engine)
            write(engine / 'Engine/Binaries/Linux/UnrealServer.target', 'fixture')
            self.assertTrue(build.check_engine(engine))
            write(engine / 'Engine/Build/Build.version', json.dumps({'MajorVersion': 5, 'MinorVersion': 8, 'PatchVersion': 2}))
            with self.assertRaisesRegex(ValueError, '5.8.3'):
                build.check_engine(engine)

    def test_installed_linux_override_can_remove_server_support(self):
        with tempfile.TemporaryDirectory() as temporary:
            engine = engine_fixture(Path(temporary))
            write(engine / 'Engine/Config/Linux/LinuxEngine.ini', '[InstalledPlatforms]\n!InstalledPlatformConfigurations=ClearArray\n')
            with self.assertRaisesRegex(ValueError, 'support'):
                build.check_engine(engine)

    def test_source_engine_needs_source_but_no_installed_override(self):
        with tempfile.TemporaryDirectory() as temporary:
            engine = engine_fixture(Path(temporary), server=False)
            (engine / 'Engine/Build/InstalledBuild.txt').unlink()
            with self.assertRaisesRegex(ValueError, 'source engine'):
                build.check_engine(engine)
            write(engine / 'Engine/Source/Runtime/Engine/Engine.Build.cs', 'fixture')
            self.assertFalse(build.check_engine(engine))

    def test_commands_build_editor_then_development_server_without_map_override(self):
        commands = build.build_commands(Path('/engine with spaces'), Path('/run/project'), Path('/run/archive'), True)
        self.assertEqual(commands[0][2:5], ['RespawnAndRageEditor', 'Linux', 'Development'])
        self.assertIn('-target=RespawnAndRageServer', commands[1])
        for flag in ('-server', '-noclient', '-build', '-cook', '-stage', '-archive', '-serverconfig=Development', '-installed'):
            self.assertIn(flag, commands[1])
        self.assertFalse(any(arg.startswith('-map=') for command in commands for arg in command))
        self.assertNotIn('-installed', build.build_commands(Path('/engine'), Path('/project'), Path('/archive'), False)[1])

    def test_map_list_comes_from_packaging_settings_and_requires_assets(self):
        with tempfile.TemporaryDirectory() as temporary:
            project = Path(temporary)
            config = write(project / 'Config/DefaultGame.ini', '[Other]\n+MapsToCook=(FilePath="/Game/Ignored")\n'
                           '[/Script/UnrealEd.ProjectPackagingSettings]\n'
                           '+MapsToCook=(FilePath="/Game/Maps/First")\n+MapsToCook=(FilePath="/Game/Maps/Second")\n'
                           '-MapsToCook=(FilePath="/Game/Maps/First")\n')
            with self.assertRaisesRegex(ValueError, 'Missing MapsToCook'):
                build.maps_to_cook(project)
            write(project / 'Content/Maps/Second.umap', 'fixture')
            self.assertEqual(build.maps_to_cook(project), ['/Game/Maps/Second'])
            config.write_text('[Other]\n+MapsToCook=(FilePath="/Game/Maps/Second")\n')
            with self.assertRaisesRegex(ValueError, 'empty'):
                build.maps_to_cook(project)

    def test_check_never_runs_build_or_creates_output(self):
        with tempfile.TemporaryDirectory() as temporary:
            output = Path(temporary) / 'not-created'
            plan = {'packaged': False, 'commands': []}
            with patch.object(build, 'preflight', return_value=plan) as preflight, \
                    patch.object(build, 'execute') as execute, patch.object(build.subprocess, 'run') as run, \
                    contextlib.redirect_stdout(io.StringIO()):
                self.assertEqual(build.main(['--engine', temporary, '--output', str(output), '--check']), 0)
            preflight.assert_called_once()
            execute.assert_not_called()
            run.assert_not_called()
            self.assertFalse(output.exists())

    def test_check_cli_rejects_unsupported_installed_engine_before_mutation(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            engine = engine_fixture(root, server=False)
            output = root / 'not-created'
            with patch.object(build.platform, 'system', return_value='Linux'), \
                    patch.object(build.platform, 'machine', return_value='x86_64'), \
                    patch.object(build, 'execute') as execute, \
                    patch.object(build.subprocess, 'run') as run, contextlib.redirect_stderr(io.StringIO()) as error:
                result = build.main(['--project', str(root / 'core'), '--engine', str(engine),
                                     '--output', str(output), '--check'])
            self.assertEqual(result, 1)
            self.assertIn('Development Server support', error.getvalue())
            execute.assert_not_called()
            run.assert_not_called()
            self.assertFalse(output.exists())

    def test_preflight_rejects_non_linux_host_without_writes(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            with patch.object(build.platform, 'system', return_value='Darwin'), \
                    self.assertRaisesRegex(ValueError, 'native Linux x86_64'):
                build.preflight(root / 'core', root / 'engine', root / 'output')
            self.assertFalse((root / 'output').exists())

    def test_toolchain_version_and_explicit_override_are_checked(self):
        with tempfile.TemporaryDirectory() as temporary, patch.dict(os.environ, {}, clear=True):
            engine = Path(temporary)
            write(engine / 'Engine/Config/Linux/Linux_SDK.json', '{"MainVersion":"fixture-v26"}')
            sdk = engine / 'Engine/Extras/ThirdPartyNotUE/SDKs/HostLinux/Linux_x64/fixture-v26'
            compiler = write(sdk / 'x86_64-unknown-linux-gnu/bin/clang++', 'fixture')
            compiler.chmod(0o755)
            write(sdk / 'ToolchainVersion.txt', 'wrong')
            with self.assertRaisesRegex(ValueError, 'toolchain'):
                build.check_toolchain(engine)
            (sdk / 'ToolchainVersion.txt').write_text('fixture-v26\n')
            self.assertEqual(build.check_toolchain(engine), str(compiler))
            with patch.dict(os.environ, {'LINUX_MULTIARCH_ROOT': str(engine / 'missing')}), \
                    self.assertRaisesRegex(ValueError, 'toolchain'):
                build.check_toolchain(engine)

    def test_package_requires_server_elf_cooked_pak_and_server_receipt(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            archive = root / 'archive'
            package = archive / 'LinuxServer'
            binary = elf_fixture(package / 'RespawnAndRage/Binaries/Linux/RespawnAndRageServer')
            receipt = write(root / 'project/Binaries/Linux/RespawnAndRageServer.target', json.dumps({
                'TargetName': 'RespawnAndRageServer', 'Platform': 'Linux', 'Configuration': 'Development', 'TargetType': 'Server'}))
            with self.assertRaisesRegex(ValueError, 'cooked content'):
                build.validate_package(archive, receipt)
            write(package / 'RespawnAndRage/Content/Paks/fixture.pak', 'fixture')
            self.assertEqual(build.validate_package(archive, receipt), (package, binary))
            receipt.write_text(receipt.read_text().replace('"Server"', '"Game"'))
            with self.assertRaisesRegex(ValueError, 'Development Server'):
                build.validate_package(archive, receipt)


class ServerLauncherTests(unittest.TestCase):
    def test_rejects_client_wrong_architecture_and_truncated_binary(self):
        with tempfile.TemporaryDirectory() as temporary:
            directory = Path(temporary) / 'Binaries/Linux'
            with self.assertRaisesRegex(ValueError, 'Server Development'):
                launch.validate_binary(elf_fixture(directory / 'RespawnAndRage'))
            binary = elf_fixture(directory / 'RespawnAndRageServer', machine=183)
            with self.assertRaisesRegex(ValueError, 'x86_64'):
                launch.validate_binary(binary)
            elf_fixture(binary)
            binary.write_bytes(binary.read_bytes()[:64])
            with self.assertRaisesRegex(ValueError, 'program table'):
                launch.validate_binary(binary)
            binary.write_bytes(b'MZ' + bytes(126))
            with self.assertRaisesRegex(ValueError, 'ELF64'):
                launch.validate_binary(binary)

    def test_direct_ip_defaults_60_and_eos_url_and_extra_args_pass_through(self):
        with tempfile.TemporaryDirectory() as temporary:
            package = Path(temporary)
            binary = elf_fixture(package / 'RespawnAndRage/Binaries/Linux/RespawnAndRageServer')
            settings = {'binary': str(binary.relative_to(package)), 'map': '/Game/Maps/Fixture',
                        'maps': ['/Game/Maps/Fixture'], 'port': 7777, 'tick_rate': 60}
            command = launch.command(package, settings)
            self.assertEqual(command[1], '/Game/Maps/Fixture')
            self.assertNotIn('EROnline=1', command[1])
            for driver in ('fdm.ERNetDriverEOS', 'OnlineSubsystemUtils.IpNetDriver'):
                for key in ('NetServerMaxTickRate', 'MaxNetTickRate'):
                    self.assertIn(f'[/Script/{driver}]:{key}=60', command[-1])
            command = launch.command(package, settings, '/Game/Maps/Fixture?EROnline=1?Seed=73',
                                     port=7788, tick_rate=30, extra=['-custom=value with spaces'])
            self.assertEqual(command[1], '/Game/Maps/Fixture?EROnline=1?Seed=73')
            self.assertIn('-port=7788', command)
            self.assertEqual(command[-1], '-custom=value with spaces')
            self.assertIn('MaxNetTickRate=30', command[-2])
            with self.assertRaisesRegex(ValueError, 'MapsToCook'):
                launch.command(package, settings, '/Game/NotCooked')
            with self.assertRaises(ValueError):
                launch.command(package, settings, tick_rate=0)

    def test_launcher_check_does_not_start_a_process(self):
        with tempfile.TemporaryDirectory() as temporary:
            package = Path(temporary)
            binary = elf_fixture(package / 'RespawnAndRage/Binaries/Linux/RespawnAndRageServer')
            config = write(package / 'server-settings.json', json.dumps({
                'binary': str(binary.relative_to(package)), 'map': '/Game/Test',
                'maps': ['/Game/Test'], 'port': 7777, 'tick_rate': 60}))
            with patch.object(launch.os, 'execv') as start, contextlib.redirect_stdout(io.StringIO()):
                self.assertEqual(launch.main(['--config', str(config), '--check', '--', '-SomeSetting=1']), 0)
            start.assert_not_called()


if __name__ == '__main__':
    unittest.main()
