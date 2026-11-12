#!/usr/bin/env python3
"""Check proportional HUD layout; --render also resizes a real Metal viewport."""
import argparse
import os
from pathlib import Path
import re
import subprocess
import time

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--render', action='store_true')
parser.add_argument('--weapons', action='store_true', help='Cycle selected items and empty ammunition during the render gallery')
countdown = parser.add_mutually_exclusive_group()
countdown.add_argument('--countdown', action='store_true', help='Keep the pre-match countdown active for the whole render gallery')
countdown.add_argument('--countdown-sprint', action='store_true', help='Check held sprint across the countdown unlock')
countdown.add_argument('--countdown-posture', action='store_true', help='Check held walk and crouch across the countdown unlock')
args = parser.parse_args()
if (args.countdown or args.countdown_sprint or args.countdown_posture) and not args.render:
    parser.error('countdown checks require --render')
root = Path(__file__).resolve().parents[1]
output = root / 'Saved/Testing/HUD'
output.mkdir(parents=True, exist_ok=True)
editor = Path(os.environ.get('UE_ROOT', '/Users/Shared/Epic Games/UE_5.8')) / 'Engine/Binaries/Mac/UnrealEditor.app/Contents/MacOS/UnrealEditor'
common = [str(editor), str(root / 'RespawnAndRage.uproject'), '-unattended', '-nosound', '-NoSplash', '-Multiprocess', '-stdout', '-FullStdOutLogOutput']
log = output / ('render.log' if args.render else 'automation.log')
extra = (['/Game/EclipseRecoil/Maps/L_Depot?Name=HUD?Team=0', '-game', '-windowed', '-RenderOffscreen', '-ForceRes', '-ResX=3840', '-ResY=2160', '-ERHUDGallery', '-ExecCmds=t.MaxFPS 60'] if args.render else
         ['-nullrhi', '-ExecCmds=Automation RunTests EclipseRecoil.Player.HUDViewportLayout', '-TestExit=Automation Test Queue Empty'])
if args.weapons:
    assert args.render, '--weapons requires --render'
    extra.append('-ERWeaponHUDGallery')
if args.countdown:
    extra.append('-ERMatchCountdown=60')
if args.countdown_sprint or args.countdown_posture:
    extra.append('-ERMatchCountdown=12')
if args.countdown_posture:
    extra.append('-ERCountdownPostureProbe')
started = time.time()
with log.open('w') as handle:
    result = subprocess.run(common + extra, stdout=handle, stderr=subprocess.STDOUT, timeout=180)
text = log.read_text(errors='replace')
if args.render:
    samples = re.findall(r'ER_HUD_GALLERY index=(\d+) scoreboard=(\d) viewport=(\d+)x(\d+) scale=([\d.]+) radius=([\d.]+) edge=([\d.]+),([\d.]+)', text)
    passed = len(samples) == 14 and 'ER_HUD_GALLERY_COMPLETE' in text
    if args.countdown:
        passed &= 'ER_MATCH_COUNTDOWN seconds=60.0' in text and 'ER_HUD_COUNTDOWN_FROZEN' not in text
    if args.countdown_sprint:
        passed &= 'ER_HUD_COUNTDOWN_FROZEN moved=0.00' in text and 'ER_HUD_COUNTDOWN_SPRINT PASS' in text
    if args.countdown_posture:
        passed &= all(marker in text for marker in ('ER_HUD_COUNTDOWN_WALK PASS', 'ER_HUD_COUNTDOWN_CROUCH PASS'))
    sizes = set()
    for index, scoreboard, w, h, scale, radius, x, y in samples:
        w, h = int(w), int(h)
        sizes.add((w, h))
        expected = min(w/1280, h/720)
        passed &= abs(float(radius)-97.92*expected) < .02
        passed &= abs(float(x)-w*40/1280) < .02 and abs(float(y)-h*40/720) < .02
        capture = output / f'{index}-{"scoreboard" if scoreboard == "1" else "hud"}.png'
        passed &= capture.exists() and capture.stat().st_mtime >= started
        print(f'{index} scoreboard={scoreboard} {w}x{h} scale={scale} radius={radius}')
    passed &= len(sizes) == 7
    if args.weapons:
        passed &= len(re.findall(r'ER_WEAPON_HUD_GALLERY index=', text)) == 7
        passed &= bool(re.search(r'ER_WEAPON_HUD_GALLERY index=6 .*loaded=0 reserve=0', text))
else:
    passed = 'Result={Success} Name={HUDViewportLayout}' in text and 'Result={Fail}' not in text
    print('\n'.join(line for line in text.splitlines() if 'Result={' in line or 'Error:' in line))
passed &= result.returncode == 0 and 'Fatal error' not in text and 'Critical error' not in text
print('HUD', 'PASS' if passed else 'FAIL', log)
raise SystemExit(0 if passed else 1)
