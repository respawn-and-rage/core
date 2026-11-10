#!/usr/bin/env python3
"""Launch a packaged dedicated server; settings belong to this package only.

By default the server advertises itself through Epic Online Services and admits only
EOS players. It uses the public, revocable Community EOS client unless all RR_EOS_*
environment variables name another one; the credential never comes from the cooked
package. --direct-ip starts an unadvertised server without EOS.
"""
import argparse
import json
import os
from pathlib import Path
import platform
import struct
import sys


def validate_binary(binary):
    """Reject client executables, wrong architectures and non-executable files."""
    if binary.name not in ('RespawnAndRageServer', 'RespawnAndRageServer-Linux-Development'):
        raise ValueError('Expected a RespawnAndRageServer Development executable')
    if binary.parent.name != 'Linux' or binary.parent.parent.name != 'Binaries':
        raise ValueError('Server executable must be in Binaries/Linux')
    with binary.open('rb') as stream:
        header = stream.read(64)
    if (len(header) != 64 or header[:7] != b'\x7fELF\x02\x01\x01'
            or struct.unpack_from('<HH', header, 16) not in ((2, 62), (3, 62))
            or struct.unpack_from('<I', header, 20)[0] != 1
            or struct.unpack_from('<H', header, 52)[0] != 64):
        raise ValueError('Server executable must be ELF64 little-endian x86_64')
    entry, table_offset = struct.unpack_from('<QQ', header, 24)
    stride, count = struct.unpack_from('<HH', header, 54)
    if stride != 56 or count == 0 or table_offset < 64 or table_offset + stride * count > binary.stat().st_size:
        raise ValueError('Server ELF program table is missing or truncated')
    executable_entry = False
    with binary.open('rb') as stream:
        stream.seek(table_offset)
        for _ in range(count):
            kind, flags, offset, address, _, file_size, memory_size, _ = struct.unpack('<IIQQQQQQ', stream.read(stride))
            if kind == 1:
                if file_size > memory_size or offset + file_size > binary.stat().st_size:
                    raise ValueError('Server ELF load segment is truncated')
                executable_entry |= bool(flags & 1) and address <= entry < address + memory_size
    if not executable_entry:
        raise ValueError('Server ELF has no executable entry point')
    if not os.access(binary, os.X_OK):
        raise ValueError('Server executable has no execute permission')
    return binary


EOS_VARIABLES = ('RR_EOS_PRODUCT_ID', 'RR_EOS_SANDBOX_ID', 'RR_EOS_DEPLOYMENT_ID',
                 'RR_EOS_CLIENT_ID', 'RR_EOS_CLIENT_SECRET')


# Community dedicated-server client: published on purpose so anyone can host a
# server. Its policy only manages dedicated sessions; abuse is handled by revoking
# the secret and publishing a new image. Never the players' client credential.
COMMUNITY_CREDENTIAL = {
    'RR_EOS_PRODUCT_ID': '27336dbd08d749668ca9a0d31c97fd60',
    'RR_EOS_SANDBOX_ID': 'db1678e6f21644559b70b6d0aaffbfdb',
    'RR_EOS_DEPLOYMENT_ID': 'd494e2dc1e974b2b9a79068b4e4080e0',
    'RR_EOS_CLIENT_ID': 'xyza78915lFSyqwiqTQuBoc2KCdh0pbB',
    'RR_EOS_CLIENT_SECRET': 'kCEiOeY+vWoEz7q6C/uSKcSgtumfni2pTUSsUBu7HUA',
}


def missing_credentials(environment):
    """Names of RR_EOS_* variables that are absent or blank (values are never returned)."""
    return [name for name in EOS_VARIABLES if not environment.get(name, '').strip()]


def eos_credential(environment):
    """Return (variables to add, source): all five RR_EOS_* or none (Community)."""
    missing = missing_credentials(environment)
    if len(missing) == len(EOS_VARIABLES):
        return dict(COMMUNITY_CREDENTIAL), 'community'
    if missing:
        raise ValueError('Incomplete EOS server credential: set ' + ', '.join(missing) +
                         ', or unset every RR_EOS_* variable to use the Community client')
    return {}, 'custom'


def positive_integer(value, maximum):
    if isinstance(value, bool):
        raise ValueError('Expected an integer')
    integer = int(value)
    if str(integer) != str(value) or not 1 <= integer <= maximum:
        raise ValueError(f'Expected an integer between 1 and {maximum}')
    return integer


def command(package, settings, map_url=None, port=None, tick_rate=None, extra=(), online=True):
    binary = (package / settings['binary']).resolve()
    if not binary.is_relative_to(package.resolve()):
        raise ValueError('Server executable must stay inside its package')
    validate_binary(binary)
    url = map_url or settings['map']
    if not isinstance(url, str) or url.split('?', 1)[0] not in settings['maps'] or any(c.isspace() for c in url):
        raise ValueError('Map URL must name a map from this package MapsToCook')
    listen_port = positive_integer(port if port is not None else settings['port'], 65535)
    rate = positive_integer(tick_rate if tick_rate is not None else settings['tick_rate'], 240)
    # Apply only to this process. Both the project's EOS/IP passthrough driver and
    # its IpNetDriver fallback need the dedicated-server and replication caps.
    fields = [f'[{section}]:{key}={rate}'
              for section in ('/Script/fdm.ERNetDriverEOS', '/Script/OnlineSubsystemUtils.IpNetDriver')
              for key in ('NetServerMaxTickRate', 'MaxNetTickRate')]
    return [str(binary), url, f'-port={listen_port}', '-log', '-unattended', '-nosound', '-nullrhi',
            *(['-EROnlineServer'] if online else []), '-ini:Engine:' + ','.join(fields), *extra]


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--config', type=Path, default=Path(__file__).with_name('server-settings.json'))
    parser.add_argument('--map', dest='map_url', help='Cooked map URL; URL options are preserved')
    parser.add_argument('--port', type=int)
    parser.add_argument('--tick-rate', type=int)
    parser.add_argument('--direct-ip', action='store_true',
                        help='Do not use EOS: no server-browser entry, players connect by IP only')
    parser.add_argument('--name', help='Server browser name (default: RR_SERVER_NAME, else "Respawn & Rage")')
    parser.add_argument('--check', action='store_true', help='Validate and print the command without starting a server')
    args, extra = parser.parse_known_args(argv)
    if extra[:1] == ['--']:
        extra = extra[1:]
    try:
        settings_path = args.config.resolve()
        settings = json.loads(settings_path.read_text())
        online = not args.direct_ip
        if online:
            defaults, credential = eos_credential(os.environ)
            os.environ.update(defaults)
            if args.name is not None:
                if not args.name.strip():
                    raise ValueError('--name must not be blank')
                os.environ['RR_SERVER_NAME'] = args.name.strip()
        elif args.name is not None:
            raise ValueError('--name needs EOS mode; it cannot be combined with --direct-ip')
        launch = command(settings_path.parent, settings, args.map_url, args.port, args.tick_rate, extra, online)
        if args.check:
            # The credential stays in the environment: only the mode and name are shown.
            print(json.dumps({'command': launch, 'online': online,
                              'credential': credential if online else None,
                              'server_name': os.environ.get('RR_SERVER_NAME') if online else None,
                              'started': False}, indent=2))
            return 0
        if platform.system() != 'Linux' or platform.machine().lower() not in ('x86_64', 'amd64'):
            raise ValueError('Run this package on native Linux x86_64')
        os.chdir(settings_path.parent)
        os.execv(launch[0], launch)
    except (ValueError, KeyError, TypeError, OSError) as error:
        print(str(error), file=sys.stderr)
        return 1
    return 0


if __name__ == '__main__':
    sys.exit(main())
