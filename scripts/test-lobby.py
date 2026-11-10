#!/usr/bin/env python3
"""Real LAN discovery, strict-majority lobby and seamless loadout travel tests."""
from pathlib import Path
import argparse
import os
import subprocess
import time

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--industrial', action='store_true', help='Vote for the generated Central Building instead of Middle Eastern Alleys')
parser.add_argument('--village', action='store_true', help='Vote for Village and verify its generated geometry on every peer')
parser.add_argument('--rooms', action='store_true', help='Vote for Rooms and verify its generated bases on every peer')
parser.add_argument('--map-sizing', action='store_true', help='Creation-time sizing, frozen lobby snapshot, independent capacity and travel descriptor')
parser.add_argument('--skull-hunt', action='store_true', help='Verify the mode vote and Skull Hunt scoring over LAN')
parser.add_argument('--timeout', action='store_true', help='Also check the real 30-second no-server timeout')
parser.add_argument('--settings', action='store_true', help='Check fullscreen defaults and preferences across two graphical launches')
parser.add_argument('--focus', action='store_true', help='Graphical foreground fullscreen/menu/game mouse capture recovery probe')
parser.add_argument('--gallery', action='store_true', help='Capture the menu pages using Metal')
args = parser.parse_args()
if sum((args.industrial, args.village, args.rooms)) > 1:
    parser.error('Choose one of --industrial, --village or --rooms')
voted_map = 4 if args.rooms else 5 if args.village else 0 if args.industrial else 2
root = Path(__file__).resolve().parents[1]
exe = Path(os.environ.get('UE_ROOT', '/Users/Shared/Epic Games/UE_5.8')) / 'Engine/Binaries/Mac/UnrealEditor.app/Contents/MacOS/UnrealEditor'
out = root / 'Saved/Testing' / ('SkullHunt' if args.skull_hunt else 'Lobby')
out.mkdir(parents=True, exist_ok=True)
common = [str(exe), str(root / 'RespawnAndRage.uproject'), '-game', '-nosound', '-unattended', '-NoSplash', '-Multiprocess', '-stdout', '-FullStdOutLogOutput', '-ExecCmds=t.MaxFPS 60,sg.GlobalIlluminationQuality 0,sg.ReflectionQuality 0,sg.ShadowQuality 1']
if args.map_sizing:
    common.append('-ERMapSizingLobbyTest')
if args.skull_hunt:
    common.append('-ERSkullHuntProbe')
common.append(f'-ERLobbyProbeMap={voted_map}')
children, handles = [], []

def launch(role, extra, user=None):
    handle = (out / (role + '.log')).open('w')
    handles.append(handle)
    process = subprocess.Popen(common + ['/Game/EclipseRecoil/Maps/L_MainMenu', '-UserDir=' + str(out / ('user-' + (user or role)))] + extra, stdout=handle, stderr=subprocess.STDOUT)
    children.append(process)
    return process

def log(role):
    return (out / (role + '.log')).read_text(errors='replace')

def wait_for(role, process, marker, timeout=75):
    end = time.monotonic() + timeout
    while marker not in log(role):
        if 'ER_LOBBY_PROBE_COMPLETE result=FAIL' in log(role):
            raise RuntimeError(f'{role}: probe failed; inspect {out / (role + ".log")}')
        if process.poll() is not None or time.monotonic() > end:
            raise RuntimeError(f'{role}: missing {marker}; inspect {out / (role + ".log")}')
        time.sleep(.2)

try:
    if args.focus:
        # Foreground activation is part of this test; unattended/multiprocess
        # launches deliberately suppress it on macOS.
        common.remove('-unattended')
        common.remove('-Multiprocess')
        user = 'focus-' + str(time.time_ns())
        process = launch('focus', ['-ERWindowInputProbe'], user=user)
        process.wait(timeout=90)
        output = log('focus')
        print('\n'.join(line for line in output.splitlines() if 'ER_LOBBY_' in line or 'ER_WINDOW_INPUT' in line), flush=True)
        assert process.returncode == 0 and 'ER_WINDOW_INPUT_COMPLETE result=PASS' in output
    elif args.settings:
        # Fresh isolated user settings ensure the default is actually tested.
        import shutil
        shutil.rmtree(out / 'user-settings', ignore_errors=True)
        for mode in ('write', 'read'):
            process = launch('settings-' + mode, ['-MultiprocessSaveConfig', '-ERSettings' + mode.title()], user='settings')
            process.wait(timeout=90)
            assert process.returncode == 0 and 'ER_SETTINGS_COMPLETE ' + mode + ' result=PASS' in log('settings-' + mode)
            print('\n'.join(line for line in log('settings-' + mode).splitlines() if 'ER_LOBBY_' in line or 'ER_SETTINGS_' in line), flush=True)
        print('Settings across restarts: PASS', flush=True)
    elif args.gallery:
        gallery = launch('gallery', ['-ERMenuGallery', '-windowed', '-ResX=1280', '-ResY=720', '-port=17949', '-ERDiscoveryPort=17950'])
        gallery.wait(timeout=150)
        assert gallery.returncode == 0 and 'ER_MENU_GALLERY_COMPLETE' in log('gallery')
        print('Metal menu gallery:', out / 'user-gallery/Saved/Screenshots/Menu')
    else:
        ports = ['-port=17941', '-ERDiscoveryPort=17942', '-nullrhi']
        host = launch('host', ports + ['-ERLobbyProbeHost'])
        wait_for('host', host, 'ER_LAN_BEACON bound=0.0.0.0:17942')
        sockets = subprocess.check_output(['/usr/sbin/lsof', '-nP', '-a', '-p', str(host.pid), '-iUDP'], text=True)
        assert '*:17941' in sockets and '*:17942' in sockets, sockets
        print('Server game/discovery sockets bound to 0.0.0.0', flush=True)
        clients = [launch('client' + str(i), ['-nullrhi', '-ERDiscoveryPort=17942', '-ERLobbyProbeClient=' + str(i)]) for i in (1, 2)]
        for role, process in [('host', host), ('client1', clients[0]), ('client2', clients[1])]:
            wait_for(role, process, 'ER_LOBBY_PROBE_COMPLETE result=PASS', 100)
            output = log(role)
            assert 'ER_LOBBY_FAIL' not in output, role
            print('\n'.join(line for line in output.splitlines() if 'ER_LOBBY_' in line or 'ER_LAN_FOUND' in line), flush=True)
        assert 'one_of_three_ready_does_not_start' in log('host')
        assert f'ER_LOBBY_START humans=3 ready=2 map={voted_map}' in log('host')
        assert all('ER_LAN_FOUND' in log('client' + str(i)) for i in (1, 2))
        if args.skull_hunt:
            for process in [*clients, host]:
                assert process.wait(timeout=20) == 0, f'Unreal process {process.pid} failed during shutdown'
        if args.timeout:
            timeout = launch('timeout', ['-nullrhi', '-ERDiscoveryPort=17948', '-ERLobbyProbeTimeout'])
            timeout.wait(timeout=65)
            assert timeout.returncode == 0 and 'ER_LOBBY_PASS discovery_timeout_has_actionable_status' in log('timeout')
            print('Real 30-second discovery timeout: PASS', flush=True)
        print('LAN lobby integration: PASS', flush=True)
finally:
    for process in children:
        if process.poll() is None:
            process.terminate()
    for process in children:
        try:
            process.wait(timeout=10)
        except subprocess.TimeoutExpired:
            process.kill()
            process.wait()
    for handle in handles:
        handle.close()
