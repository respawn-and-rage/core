"""Tooling contracts for the server Docker image; no Unreal or Docker is run here."""
import importlib.util
import io
import json
import os
from pathlib import Path
import subprocess
import tempfile
import unittest
from unittest import mock
import urllib.error

spec = importlib.util.spec_from_file_location('server_image', Path(__file__).parents[1] / 'server-image.py')
image = importlib.util.module_from_spec(spec)
spec.loader.exec_module(image)
fixtures_spec = importlib.util.spec_from_file_location('server_fixtures', Path(__file__).with_name('test_build_server_linux.py'))
fixtures = importlib.util.module_from_spec(fixtures_spec)
fixtures_spec.loader.exec_module(fixtures)
write, elf_fixture = fixtures.write, fixtures.elf_fixture

VERSION = '26.11.09.abcdef12'
TOKEN = 'dckr_pat_secret-value'


def windows_engine(root, server=True, required_file='', installed=True):
    engine = root / 'engine with spaces'
    write(engine / 'Engine/Build/Build.version', json.dumps({'MajorVersion': 5, 'MinorVersion': 8, 'PatchVersion': 3}))
    for name in ('Engine/Build/BatchFiles/Build.bat', 'Engine/Build/BatchFiles/RunUAT.bat',
                 'Engine/Binaries/ThirdParty/DotNet/10.0/win-x64/dotnet.exe'):
        write(engine / name, 'fixture')
    if not installed:
        write(engine / 'Engine/Source/Runtime/Engine/Engine.Build.cs', 'fixture')
        return engine
    write(engine / 'Engine/Build/InstalledBuild.txt', '')
    write(engine / 'Engine/Binaries/Win64/UnrealEditor-Cmd.exe', 'fixture')
    write(engine / 'Engine/Binaries/Win64/Platforms/UnrealEditor-LinuxTargetPlatform.dll', 'fixture')
    declarations = '[InstalledPlatforms]\nHasInstalledPlatformInfo=true\n'
    for target in ('Editor', 'Server') if server else ('Editor', 'Game'):
        declarations += ('+InstalledPlatformConfigurations=(PlatformName="Linux", Configuration="Development", '
                         f'PlatformType="{target}", Architecture="x64", RequiredFile="{required_file}")\n')
    write(engine / 'Engine/Config/BaseEngine.ini', declarations)
    return engine


def project_fixture(root):
    project = root / 'project'
    write(project / 'Config/DefaultGame.ini', '[/Script/UnrealEd.ProjectPackagingSettings]\n'
          '+MapsToCook=(FilePath="/Game/EclipseRecoil/Maps/L_CentralBuilding")\n'
          '+MapsToCook=(FilePath="/Game/EclipseRecoil/Maps/L_Town")\n')
    write(project / 'Config/DefaultEngine.ini', '+NetDriverDefinitions=(DefName="GameNetDriver",'
          'DriverClassName="/Script/fdm.ERNetDriverEOS",DriverClassNameFallback="/Script/OnlineSubsystemUtils.IpNetDriver")\n')
    for name in ('L_CentralBuilding', 'L_Town'):
        write(project / f'Content/EclipseRecoil/Maps/{name}.umap', 'fixture')
    return project


def archive_fixture(root, version=VERSION):
    """A Windows-style BuildCookRun archive: CRLF launcher, server ELF, pak, receipt."""
    archive = root / 'server attempt/dist'
    package = archive / 'LinuxServer'
    elf_fixture(package / 'RespawnAndRage/Binaries/Linux/RespawnAndRageServer')
    write(package / 'RespawnAndRage/Content/Paks/RespawnAndRage-LinuxServer.pak', 'cooked')
    write(package / 'RespawnAndRage/Build/GameVersion.txt', version + '\n')
    (package / 'RespawnAndRageServer.sh').write_bytes(b'#!/bin/sh\r\nexit 0\r\n')
    receipt = write(root / 'workspace/Binaries/Linux/RespawnAndRageServer.target', json.dumps({
        'TargetName': 'RespawnAndRageServer', 'Platform': 'Linux', 'Configuration': 'Development', 'TargetType': 'Server'}))
    return archive, package, receipt


class Recorder:
    def __init__(self, results=None):
        self.calls, self.results = [], results or {}

    def __call__(self, command, **options):
        self.calls.append((command, options))
        key = tuple(command[1:3])
        code, stdout = self.results.get(key, (0, ''))
        if options.get('check') and code:
            raise subprocess.CalledProcessError(code, command)
        return subprocess.CompletedProcess(command, code, stdout, 'daemon failure' if code else '')


class FakeResponse(io.BytesIO):
    def __enter__(self):
        return self

    def __exit__(self, *args):
        self.close()


def hub(login_code=200, repo_code=200, private=False):
    requests = []

    def opener(request, timeout):
        requests.append(request)
        if request.full_url.endswith('/v2/auth/token'):
            if login_code != 200:
                raise urllib.error.HTTPError(request.full_url, login_code, 'denied', {}, None)
            return FakeResponse(json.dumps({'access_token': 'jwt'}).encode())
        if repo_code != 200:
            raise urllib.error.HTTPError(request.full_url, repo_code, 'missing', {}, None)
        return FakeResponse(json.dumps({'is_private': private}).encode())
    return opener, requests


class EngineTests(unittest.TestCase):
    def test_launcher_style_engine_without_server_is_rejected(self):
        with tempfile.TemporaryDirectory() as temporary:
            engine = windows_engine(Path(temporary), server=False)
            with self.assertRaisesRegex(ValueError, 'Epic Launcher'):
                image.check_engine(engine)

    def test_installed_server_support_needs_its_required_file(self):
        with tempfile.TemporaryDirectory() as temporary:
            engine = windows_engine(Path(temporary), required_file='Engine\\Binaries\\Linux\\UnrealServer.target')
            with self.assertRaisesRegex(ValueError, 'Server targets'):
                image.check_engine(engine)
            write(engine / 'Engine/Binaries/Linux/UnrealServer.target', 'fixture')
            self.assertTrue(image.check_engine(engine))

    def test_source_engine_and_version(self):
        with tempfile.TemporaryDirectory() as temporary:
            engine = windows_engine(Path(temporary), installed=False)
            self.assertFalse(image.check_engine(engine))
            write(engine / 'Engine/Build/Build.version', json.dumps({'MajorVersion': 5, 'MinorVersion': 8, 'PatchVersion': 2}))
            with self.assertRaisesRegex(ValueError, '5.8.3'):
                image.check_engine(engine)


class CredentialTests(unittest.TestCase):
    def test_credentials_and_repository_validation(self):
        with tempfile.TemporaryDirectory() as temporary:
            path = Path(temporary) / 'dockerhub.json'
            with self.assertRaisesRegex(ValueError, 'missing'):
                image.load_credentials(path)
            path.write_text(json.dumps({'username': 'rr', 'token': TOKEN, 'repository': 'rr/respawn-and-rage-server'}))
            self.assertEqual(image.load_credentials(path)['repository'], 'rr/respawn-and-rage-server')
            self.assertEqual(image.load_credentials(path, 'other/name')['repository'], 'other/name')
            with self.assertRaisesRegex(ValueError, 'namespace/name'):
                image.load_credentials(path, 'Upper/Case')
            path.write_text(json.dumps({'username': 'rr', 'repository': 'rr/server'}))
            with self.assertRaisesRegex(ValueError, 'token'):
                image.load_credentials(path)

    def test_hub_requires_existing_public_repository(self):
        credentials = {'username': 'rr', 'token': TOKEN, 'repository': 'rr/server'}
        opener, requests = hub()
        self.assertEqual(image.check_hub(credentials, opener=opener), 'rr/server')
        self.assertEqual(json.loads(requests[0].data), {'identifier': 'rr', 'secret': TOKEN})
        self.assertEqual(requests[1].get_header('Authorization'), 'Bearer jwt')
        with self.assertRaisesRegex(ValueError, 'rejected'):
            image.check_hub(credentials, opener=hub(login_code=401)[0])
        with self.assertRaisesRegex(ValueError, 'Create it'):
            image.check_hub(credentials, opener=hub(repo_code=404)[0])
        with self.assertRaisesRegex(ValueError, 'must be public'):
            image.check_hub(credentials, opener=hub(private=True)[0])


class StageAndDockerTests(unittest.TestCase):
    def test_stage_validates_and_writes_lf_docker_context(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            project = project_fixture(root)
            archive, package, receipt = archive_fixture(root)
            result = image.stage(project, archive, receipt, VERSION)
            self.assertEqual(Path(result['package']), package)
            self.assertEqual((package / 'RespawnAndRageServer.sh').read_bytes(), b'#!/bin/sh\nexit 0\n')
            settings = json.loads((package / 'server-settings.json').read_text())
            self.assertEqual(settings['binary'], 'RespawnAndRage/Binaries/Linux/RespawnAndRageServer')
            self.assertEqual(settings['map'], image.DEFAULT_MAP)
            dockerfile = (package / 'Dockerfile').read_bytes()
            self.assertNotIn(b'\r', dockerfile)
            self.assertIn(b'USER 10001:10001', dockerfile)
            self.assertIn(b'EXPOSE 7777/udp', dockerfile)
            self.assertIn(b'**/*.debug', (package / '.dockerignore').read_bytes())
            self.assertEqual(image.validate(archive, VERSION), package)
            with self.assertRaisesRegex(ValueError, 'differs'):
                image.validate(archive, '26.11.09.00000000')

    def test_stage_rejects_mismatched_version_and_missing_receipt(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            project = project_fixture(root)
            archive, _, receipt = archive_fixture(root, version='26.01.01.00000000')
            with self.assertRaisesRegex(ValueError, 'differs'):
                image.stage(project, archive, receipt, VERSION)
            with self.assertRaisesRegex(ValueError, 'Invalid game version'):
                image.stage(project, archive, receipt, 'latest')
            receipt.unlink()
            with self.assertRaisesRegex(ValueError, 'receipt'):
                image.stage(project, archive, receipt, '26.01.01.00000000')

    def test_check_docker_requires_linux_daemon_and_buildx(self):
        docker = os.devnull if os.name != 'nt' else 'NUL'
        with mock.patch.object(image.shutil, 'which', return_value=docker):
            image.check_docker('docker', run=Recorder({('version', '--format'): (0, 'linux\n')}))
            with self.assertRaisesRegex(ValueError, 'Linux containers'):
                image.check_docker('docker', run=Recorder({('version', '--format'): (0, 'windows\n')}))
            with self.assertRaisesRegex(ValueError, 'unreachable'):
                image.check_docker('docker', run=Recorder({('version', '--format'): (1, '')}))
            with self.assertRaisesRegex(ValueError, 'buildx'):
                image.check_docker('docker', run=Recorder({('version', '--format'): (0, 'linux'),
                                                           ('buildx', 'version'): (1, '')}))

    def test_build_tags_version_and_latest_for_amd64(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            archive, package, receipt = archive_fixture(root)
            image.stage(project_fixture(root), archive, receipt, VERSION)
            run = Recorder()
            tags = image.build('docker', archive, 'rr/server', VERSION, 'c0ffee', run=run)
            self.assertEqual(tags, ['rr/server:' + VERSION, 'rr/server:latest'])
            command = run.calls[0][0]
            self.assertEqual(command[:3], ['docker', 'buildx', 'build'])
            self.assertIn('linux/amd64', command)
            self.assertIn('org.opencontainers.image.revision=c0ffee', command)
            self.assertEqual(command[-1], str(package))

    def test_push_uses_stdin_and_a_temporary_config(self):
        run = Recorder({('context', 'inspect'): (0, 'npipe:////./pipe/dockerDesktopLinuxEngine\n')})
        credentials = {'username': 'rr', 'token': TOKEN, 'repository': 'rr/server'}
        with mock.patch.dict(os.environ, {'DOCKER_CONTEXT': 'desktop-linux'}):
            os.environ.pop('DOCKER_HOST', None)
            image.push('docker', credentials, ['rr/server:' + VERSION, 'rr/server:latest'], run=run)
        commands = [call[0] for call in run.calls]
        self.assertTrue(all(TOKEN not in ' '.join(command) for command in commands), 'Token must never be an argument')
        login = run.calls[1]
        self.assertEqual(login[0][1:], ['login', '--username', 'rr', '--password-stdin'])
        self.assertEqual(login[1]['input'], TOKEN)
        config = login[1]['env']['DOCKER_CONFIG']
        self.assertNotIn('DOCKER_CONTEXT', login[1]['env'])
        self.assertEqual(login[1]['env']['DOCKER_HOST'], 'npipe:////./pipe/dockerDesktopLinuxEngine')
        self.assertEqual([command[1:] for command in commands[2:4]], [['push', 'rr/server:' + VERSION], ['push', 'rr/server:latest']])
        self.assertFalse(Path(config).exists(), 'Temporary Docker config must be deleted')

    def test_failed_login_stops_before_push(self):
        run = Recorder({('context', 'inspect'): (0, 'unix:///var/run/docker.sock'), ('login', '--username'): (1, '')})
        with self.assertRaisesRegex(ValueError, 'docker login failed'):
            image.push('docker', {'username': 'rr', 'token': TOKEN}, ['rr/server:latest'], run=run)
        self.assertFalse(any(call[0][1] == 'push' for call in run.calls))


if __name__ == '__main__':
    unittest.main()
