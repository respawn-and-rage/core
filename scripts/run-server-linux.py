#!/usr/bin/env python3
"""Launch a packaged dedicated server; settings belong to this package only."""
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


def positive_integer(value, maximum):
    if isinstance(value, bool):
        raise ValueError('Expected an integer')
    integer = int(value)
    if str(integer) != str(value) or not 1 <= integer <= maximum:
        raise ValueError(f'Expected an integer between 1 and {maximum}')
    return integer


def command(package, settings, map_url=None, port=None, tick_rate=None, extra=()):
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
            '-ini:Engine:' + ','.join(fields), *extra]


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--config', type=Path, default=Path(__file__).with_name('server-settings.json'))
    parser.add_argument('--map', dest='map_url', help='Cooked map URL; URL options are preserved')
    parser.add_argument('--port', type=int)
    parser.add_argument('--tick-rate', type=int)
    parser.add_argument('--check', action='store_true', help='Validate and print the command without starting a server')
    args, extra = parser.parse_known_args(argv)
    if extra[:1] == ['--']:
        extra = extra[1:]
    try:
        settings_path = args.config.resolve()
        settings = json.loads(settings_path.read_text())
        launch = command(settings_path.parent, settings, args.map_url, args.port, args.tick_rate, extra)
        if args.check:
            print(json.dumps({'command': launch, 'started': False}, indent=2))
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
