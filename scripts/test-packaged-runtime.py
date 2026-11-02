#!/usr/bin/env python3
"""Boot a Development package with a pistol-equipped player, six bots and native graphics defaults."""
import argparse
import json
from pathlib import Path
import re
import statistics
import subprocess
import sys
import tempfile

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--executable', type=Path, required=True)
parser.add_argument('--map-package', default='/Game/EclipseRecoil/Maps/L_Depot')
parser.add_argument('--listen', action='store_true', help='Start a real listen server')
parser.add_argument('--all-maps', action='store_true', help='Check every configured gameplay map offline and as a listen server')
parser.add_argument('--render', action='store_true', help='Use Metal offscreen instead of the null renderer')
parser.add_argument('--shader-model', choices=('sm5', 'sm6'), help='Force a Metal shader path (requires --render)')
parser.add_argument('--seconds', type=int, default=20)
parser.add_argument('--width', type=int, default=1920)
parser.add_argument('--height', type=int, default=1080)
parser.add_argument('--output', type=Path)
args = parser.parse_args()
if args.shader_model and not args.render:
    parser.error('--shader-model requires --render')
if args.seconds < 10:
    parser.error('Allow at least ten seconds for map loading and animation evaluation')
out = args.output or Path(__file__).resolve().parents[1] / 'Saved/Testing/packaged-runtime'
out.mkdir(parents=True, exist_ok=True)
if args.all_maps:
    config = Path(__file__).resolve().parents[1] / 'Config/DefaultGame.ini'
    maps = re.findall(r'^\+MapsToCook=\(FilePath="([^"]+)"\)', config.read_text(), re.MULTILINE)
    maps = [package for package in maps if not package.endswith('/L_MainMenu')]
    if not maps:
        raise RuntimeError('No gameplay MapsToCook configured')
    results = []
    for package in maps:
        for listen in (False, True):
            command = [sys.executable, str(Path(__file__).resolve()),
                       *[arg for arg in sys.argv[1:] if arg not in ('--all-maps', '--listen')],
                       '--map-package', package]
            if listen:
                command.append('--listen')
            code = subprocess.run(command).returncode
            results.append(dict(map=package, listen=listen, exit_code=code))
    passed = all(case['exit_code'] == 0 for case in results)
    (out / 'maps.json').write_text(json.dumps(dict(result='PASS' if passed else 'FAIL', cases=results), indent=2) + '\n')
    raise SystemExit(0 if passed else 1)
report_name = ('metal-' + args.shader_model if args.shader_model else 'metal') if args.render else 'null'
if args.map_package != '/Game/EclipseRecoil/Maps/L_Depot' or args.listen:
    report_name = args.map_package.rsplit('/', 1)[-1] + ('-listen-' if args.listen else '-offline-') + report_name
log = out / (report_name + '.log')
with tempfile.TemporaryDirectory(prefix='rr-package-user-') as user:
    command = [str(args.executable.resolve()),
               # Pin the reference arsenal so closed presets cannot override the
               # pistol selection used to catch cooked animation regressions.
               args.map_package + ('?listen' if args.listen else '') + '?ERFullArsenalTest=1?Primary=1?ERAlphaBots=3?EROmegaBots=3?ERBotMode=Target?MapSeed=42',
               '-unattended', '-nosound', '-NoSplash', '-stdout', '-FullStdOutLogOutput',
               '-UserDir=' + user, '-Seconds=' + str(args.seconds), '-ERPerformanceAudit']
    command += ['-RenderOffscreen', '-windowed', '-ResX=' + str(args.width), '-ResY=' + str(args.height)] if args.render else ['-nullrhi']
    if sys.platform == 'darwin':
        # UE 5.8 can clear LLM thread state during command-line processing on
        # the game thread while AppKit still holds an allocator LLM scope on
        # the main thread. Keep tracking enabled so that scope can pop safely.
        command.append('-llm')
    if args.shader_model:
        command.append('-' + args.shader_model)
    with log.open('w') as stream:
        process = subprocess.Popen(command, stdout=stream, stderr=subprocess.STDOUT)
        try:
            code = process.wait(timeout=args.seconds + 90)
        except subprocess.TimeoutExpired:
            process.terminate()
            try:
                process.wait(timeout=10)
            except subprocess.TimeoutExpired:
                process.kill()
                process.wait()
            raise RuntimeError('Packaged game timed out; inspect ' + str(log))
text = log.read_text(errors='replace')
checks = dict(clean_exit=code == 0,
              requested_map_loaded=('Bringing World ' + args.map_package + '.') in text,
              no_travel_failure=not any(m in text for m in ['TravelFailure', 'Travel failed', 'Failed to load package', 'InvalidURL']),
              native_graphics_profile='ER_RENDER_PROFILE aa=2 gi=0 reflections=0 shadows=1' in text,
              pistol_evaluated='ER_WEAPON_MODEL name=Pistol' in text,
              six_bots=text.count('ER_BOT_JOINED') >= 6,
              no_fatal=not any(m in text for m in ['Assertion failed', 'Fatal error', 'Critical error']))
if args.listen:
    checks['listen_server'] = bool(re.search(r'GameNetDriver.*listening on port', text))
fps = [int(n) for n in re.findall(r'ER_PERFORMANCE (\d+) FPS', text)]
if args.render:
    checks['real_frames'] = len(fps) >= 5
selected = re.search(r'Metadata set : shaderplatform="METAL_(SM[56])"', text)
selected_shader_model = selected.group(1).lower() if selected else None
if args.shader_model:
    checks['requested_shader_model'] = selected_shader_model == args.shader_model
report = dict(result='PASS' if all(checks.values()) else 'FAIL', exit_code=code, checks=checks,
              renderer='Metal' if args.render else 'null',
              shader_model=args.shader_model,
              selected_shader_model=selected_shader_model,
              fps_median=statistics.median(fps[5:]) if args.render and len(fps) > 5 else None,
              log=str(log), command=command)
(out / (report_name + '.json')).write_text(json.dumps(report, indent=2) + '\n')
print(json.dumps(report, indent=2))
raise SystemExit(0 if all(checks.values()) else 1)
