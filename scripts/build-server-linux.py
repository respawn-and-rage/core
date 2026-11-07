#!/usr/bin/env python3
"""Prepare a local Development dedicated-server package on native Linux x86_64.

--check reads prerequisites and prints the exact plan without invoking Unreal or
writing a workspace. No engine overrides, downloads, uploads or cloud operations.
"""
import argparse
from datetime import datetime, timezone
import hashlib
import importlib.util
import json
import os
from pathlib import Path
import platform
import re
import shutil
import subprocess
import sys
import uuid

PROJECT = Path(__file__).resolve().parents[1]
TARGET = 'RespawnAndRageServer'


def helper(filename):
    spec = importlib.util.spec_from_file_location(filename.replace('-', '_'), Path(__file__).with_name(filename))
    module = importlib.util.module_from_spec(spec)
    # --check must not create/update __pycache__ in the source checkout.
    previous = sys.dont_write_bytecode
    try:
        sys.dont_write_bytecode = True
        spec.loader.exec_module(module)
    finally:
        sys.dont_write_bytecode = previous
    return module


def installed_configurations(engine):
    """Read installed platform declarations, including Linux engine overrides."""
    records, enabled = [], False
    names = ('Engine/Config/Base.ini', 'Engine/Config/BaseEngine.ini',
             'Engine/Config/Linux/BaseLinuxEngine.ini', 'Engine/Platforms/Linux/Config/BaseLinuxEngine.ini',
             'Engine/Config/Linux/LinuxEngine.ini', 'Engine/Platforms/Linux/Config/LinuxEngine.ini')
    for name in names:
        path = engine / name
        if not path.is_file():
            continue
        section = ''
        for line in path.read_text(encoding='utf-8-sig').splitlines():
            line = line.strip()
            if line.startswith('[') and line.endswith(']'):
                section = line[1:-1]
            if section != 'InstalledPlatforms' or '=' not in line or line.startswith((';', '#')):
                continue
            key, value = (part.strip() for part in line.split('=', 1))
            if key == 'HasInstalledPlatformInfo':
                enabled = value.lower() == 'true'
            elif key == '!InstalledPlatformConfigurations':
                records.clear()
            elif key.lstrip('+.-') == 'InstalledPlatformConfigurations':
                record = {match[0]: match[1] if match[1] else match[2].strip()
                          for match in re.findall(r'(\w+)\s*=\s*(?:"([^"]*)"|([^,()]*))', value)}
                if key.startswith('-'):
                    records = [entry for entry in records if entry != record]
                elif record not in records:
                    records.append(record)
    return enabled, records


def check_engine(engine):
    version = json.loads((engine / 'Engine/Build/Build.version').read_text())
    if tuple(version.get(key) for key in ('MajorVersion', 'MinorVersion', 'PatchVersion')) != (5, 8, 3):
        raise ValueError('Dedicated server requires UE 5.8.3; select a matching engine explicitly')
    installed = (engine / 'Engine/Build/InstalledBuild.txt').is_file()
    if installed:
        enabled, records = installed_configurations(engine)
        for target_type in ('Editor', 'Server'):
            matches = [entry for entry in records
                       if entry.get('PlatformName') == 'Linux'
                       and entry.get('Configuration') == 'Development'
                       and entry.get('PlatformType', 'Game') == target_type
                       and entry.get('Architecture', '') in ('', 'x64', 'x86_64', 'x86_64-unknown-linux-gnu')
                       and entry.get('ProjectType', 'Any') in ('Any', 'Code')]
            if not enabled or not any(not entry.get('RequiredFile') or
                                      (engine / entry['RequiredFile']).is_file() for entry in matches):
                raise ValueError(f'Installed engine lacks downloaded Linux x86_64 Development {target_type} support. '
                                 'Use a source engine or an Installed Build built with server support; '
                                 'the helper will not override engine restrictions.')
    for name in ('Engine/Build/BatchFiles/Linux/Build.sh', 'Engine/Build/BatchFiles/RunUAT.sh'):
        if not (engine / name).is_file():
            raise ValueError('Missing Linux engine build tool: ' + name)
    if installed and not (engine / 'Engine/Binaries/Linux/UnrealEditor-Cmd').is_file():
        raise ValueError('Installed engine lacks Linux UnrealEditor-Cmd for cooking')
    if not installed and not (engine / 'Engine/Source/Runtime/Engine/Engine.Build.cs').is_file():
        raise ValueError('Engine is neither a supported Installed Build nor a source engine')
    if not any(path.is_file() and os.access(path, os.X_OK) for path in
               (engine / 'Engine/Binaries/ThirdParty/DotNet').glob('*/linux-x64/dotnet')):
        raise ValueError('Missing prepared Linux x86_64 bundled .NET; install engine prerequisites separately')
    return installed


def check_toolchain(engine):
    sdk = json.loads((engine / 'Engine/Config/Linux/Linux_SDK.json').read_text())
    version = sdk['MainVersion']
    roots = [engine / 'Engine/Extras/ThirdPartyNotUE/SDKs/HostLinux/Linux_x64' / version]
    if os.environ.get('UE_SDKS_ROOT'):
        roots.append(Path(os.environ['UE_SDKS_ROOT']) / 'HostLinux/Linux_x64' / version)
    if os.environ.get('LINUX_MULTIARCH_ROOT'):
        roots = [Path(os.environ['LINUX_MULTIARCH_ROOT'])]
    for root in roots:
        compiler = root / 'x86_64-unknown-linux-gnu/bin/clang++'
        marker = root / 'ToolchainVersion.txt'
        if compiler.is_file() and os.access(compiler, os.X_OK) and marker.is_file() and marker.read_text().strip() == version:
            return str(compiler)
    raise ValueError('Missing prepared native Linux toolchain ' + version + '; install engine prerequisites separately')


def maps_to_cook(project):
    section, maps = '', []
    for line in (project / 'Config/DefaultGame.ini').read_text().splitlines():
        line = line.strip()
        if line.startswith('[') and line.endswith(']'):
            section = line[1:-1]
        if section != '/Script/UnrealEd.ProjectPackagingSettings':
            continue
        if line.startswith('!MapsToCook='):
            maps.clear()
        match = re.fullmatch(r'([+.-]?)MapsToCook\s*=\s*\(FilePath="(/Game/[^"]+)"\)', line)
        if match:
            value = match[2]
            if match[1] == '-':
                maps = [name for name in maps if name != value]
            elif value not in maps:
                maps.append(value)
    if not maps:
        raise ValueError('MapsToCook is empty in Config/DefaultGame.ini')
    for name in maps:
        if '..' in Path(name).parts or not (project / 'Content' / (name.removeprefix('/Game/') + '.umap')).is_file():
            raise ValueError('Missing MapsToCook package: ' + name)
    return maps


def build_commands(engine, workspace, archive, installed):
    project_arg = '-project=' + str(workspace / 'RespawnAndRage.uproject')
    editor = ['bash', str(engine / 'Engine/Build/BatchFiles/Linux/Build.sh'), 'RespawnAndRageEditor',
              'Linux', 'Development', project_arg, '-NoHotReloadFromIDE']
    cook = ['bash', str(engine / 'Engine/Build/BatchFiles/RunUAT.sh'), 'BuildCookRun', project_arg,
            '-target=' + TARGET, '-platform=Linux', '-serverplatform=Linux', '-serverconfig=Development',
            '-server', '-noclient', '-build', '-cook', '-stage', '-pak', '-archive',
            '-archivedirectory=' + str(archive), '-nop4', '-unattended', '-utf8output', '-nullrhi']
    if installed:
        cook.append('-installed')
    return [editor, cook]


def check_transport(project):
    config = (project / 'Config/DefaultEngine.ini').read_text()
    driver = re.search(r'^\+NetDriverDefinitions=\(DefName="GameNetDriver",DriverClassName="([^"]+)",'
                       r'DriverClassNameFallback="([^"]+)"\)', config, re.MULTILINE)
    if not driver or driver.groups() != ('/Script/fdm.ERNetDriverEOS', '/Script/OnlineSubsystemUtils.IpNetDriver'):
        raise ValueError('GameNetDriver changed; review dedicated-server tick-rate and IP passthrough configuration')


def preflight(project, engine, output):
    if platform.system() != 'Linux' or platform.machine().lower() not in ('x86_64', 'amd64'):
        raise ValueError('Build and --check require a native Linux x86_64 host; cross-compilation is not supported')
    if output.is_relative_to(project) or project.is_relative_to(output):
        raise ValueError('Output must be outside the source checkout')
    if output.exists():
        raise ValueError('Output must not already exist')
    installed = check_engine(engine)
    compiler = check_toolchain(engine)
    if not (project / 'Source/RespawnAndRageServer.Target.cs').is_file():
        raise ValueError('Missing dedicated-server target')
    status = subprocess.check_output(['git', '-C', str(project), 'status', '--porcelain',
                                      '--untracked-files=all', '--ignore-submodules=none'],
                                     text=True, env={**os.environ, 'GIT_OPTIONAL_LOCKS': '0'})
    if status.strip():
        raise ValueError('Working tree is dirty; commit or stash changes including submodules before packaging')
    helper('setup-assets.py').validate(project, require_mounts=True)
    # Includes private meshes, skeletons and animation assets needed by authority
    # hitboxes. Never create a reduced Content tree for a headless package.
    helper('archive-linux-project.py').marketplace_plugins(project, engine)
    check_transport(project)
    maps = maps_to_cook(project)
    central = '/Game/EclipseRecoil/Maps/L_CentralBuilding'
    if central not in maps:
        raise ValueError('Default dedicated-server map is absent from MapsToCook')
    ancestor = output.parent
    while not ancestor.exists():
        ancestor = ancestor.parent
    if shutil.disk_usage(ancestor).free < 60 * 1024 ** 3:
        raise ValueError('At least 60 GiB free is required on the output drive')
    return {'engine': str(engine), 'installed_engine': installed, 'target': TARGET,
            'compiler': compiler,
            'platform': 'Linux', 'configuration': 'Development', 'maps_to_cook': maps,
            'default_map': central, 'output': str(output), 'packaged': False,
            'commands': build_commands(engine, output / 'project', output / 'archive', installed)}


def validate_package(archive, target_receipt):
    candidates = [path for path in archive.rglob('*') if path.is_file()
                  and path.name in (TARGET, TARGET + '-Linux-Development')
                  and path.parent.name == 'Linux' and path.parent.parent.name == 'Binaries']
    if len(candidates) != 1:
        raise ValueError('Expected exactly one staged dedicated-server executable')
    binary = helper('run-server-linux.py').validate_binary(candidates[0])
    package = binary.parents[3]
    if not package.is_relative_to(archive) or not any(path.stat().st_size > 0 for path in
                                                     (binary.parents[2] / 'Content/Paks').glob('*.pak')):
        raise ValueError('Missing staged server cooked content (.pak)')
    if not target_receipt.is_file():
        raise ValueError('Missing server build target receipt')
    receipt = json.loads(target_receipt.read_text())
    if (receipt.get('TargetName'), receipt.get('Platform'), receipt.get('Configuration'), receipt.get('TargetType')) != (
            TARGET, 'Linux', 'Development', 'Server'):
        raise ValueError('Target receipt does not identify a Linux Development Server')
    return package, binary


def execute(project, engine, output, plan):
    output.mkdir(parents=True)
    try:
        helper('prepare-desktop-build.py').prepare(project, engine, output / 'project')
        with (output / 'build.log').open('w') as log:
            for command_line in plan['commands']:
                subprocess.run(command_line, cwd=output / 'project', stdout=log, stderr=subprocess.STDOUT, check=True)
        target_receipt = output / 'project/Binaries/Linux/RespawnAndRageServer.target'
        package, binary = validate_package(output / 'archive', target_receipt)
        shutil.copy2(Path(__file__).with_name('run-server-linux.py'), package / 'run-server-linux.py')
        settings = {'binary': str(binary.relative_to(package)), 'map': plan['default_map'],
                    'maps': plan['maps_to_cook'], 'port': 7777, 'tick_rate': 60}
        (package / 'server-settings.json').write_text(json.dumps(settings, indent=2) + '\n')
        shutil.copy2(target_receipt, package / 'server-build-target.json')
        helper('run-server-linux.py').command(package, settings)
        hasher = hashlib.sha256()
        with binary.open('rb') as stream:
            for block in iter(lambda: stream.read(1024 * 1024), b''):
                hasher.update(block)
        digest = hasher.hexdigest()
        plan.update(packaged=True, package=str(package), binary_sha256=digest, runtime_verified=False)
    except Exception as error:
        plan.update(error=str(error))
        raise
    finally:
        (output / 'receipt.json').write_text(json.dumps(plan, indent=2) + '\n')


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--project', type=Path, default=PROJECT)
    parser.add_argument('--engine', type=Path, required=True, help='Explicit UE 5.8.3 engine root with Linux server support')
    parser.add_argument('--output', type=Path, help='New directory outside the source checkout')
    parser.add_argument('--check', action='store_true')
    args = parser.parse_args(argv)
    project, engine = args.project.resolve(), args.engine.resolve()
    output = (args.output or project.parent / 'builds/server-linux' /
              (datetime.now(timezone.utc).strftime('%Y%m%d-%H%M%S') + '-' + uuid.uuid4().hex[:8])).resolve()
    try:
        plan = preflight(project, engine, output)
        print(json.dumps(plan, indent=2), flush=True)
        if not args.check:
            execute(project, engine, output, plan)
            print('SERVER_PACKAGE_PASS: ' + plan['package'])
    except (ValueError, RuntimeError, KeyError, OSError, subprocess.CalledProcessError) as error:
        print(str(error), file=sys.stderr)
        return 1
    return 0


if __name__ == '__main__':
    sys.exit(main())
