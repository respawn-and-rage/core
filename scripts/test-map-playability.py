#!/usr/bin/env python3
"""Playability reports: graph unit tests, layout batch, physical stage, report-only neutrality."""
import argparse
import csv
import json
import os
from pathlib import Path
import re
import subprocess

p = argparse.ArgumentParser(description=__doc__)
p.add_argument('--batch', action='store_true', help='20 seeds per template and size: up to 300 layout builds, without the physical stage')
p.add_argument('--teams', default='2,4,8', help='Expected team sizes')
p.add_argument('--seeds', default='42,73,7919', help='Seeds, ignored by --batch')
p.add_argument('--reports', action='store_true', help='Keep one JSON report per plan')
args = p.parse_args()
root = Path(__file__).resolve().parents[1]
out = root/'Saved/Testing'
reports = out/'MapPlayability'
reports.mkdir(parents=True, exist_ok=True)
exe = Path(os.environ.get('UE_ROOT', '/Users/Shared/Epic Games/UE_5.8'))/'Engine/Binaries/Mac/UnrealEditor.app/Contents/MacOS/UnrealEditor'
seeds = ','.join(str(s) for s in range(20)) if args.batch else args.seeds


def run(label, tests, *extra):
    log = out/f'map-playability-{label}.log'
    with log.open('w') as f:
        result = subprocess.run([str(exe), str(root/'RespawnAndRage.uproject'), '/Engine/Maps/Entry', '-nullrhi', '-nosound', '-unattended', '-NoSplash', '-Multiprocess', '-stdout', '-FullStdOutLogOutput',
            f'-ExecCmds=Automation RunTests {tests}', '-TestExit=Automation Test Queue Empty', f'-ERPlayabilityTeams={args.teams}', f'-ERPlayabilitySeeds={seeds}', *extra], stdout=f, stderr=subprocess.STDOUT, cwd=root)
    text = log.read_text(errors='replace')
    passed = result.returncode == 0 and 'Result={Success}' in text and 'Result={Fail}' not in text
    print('\n'.join(line for line in text.splitlines() if 'Result={' in line or ('Error:' in line and 'Condition failed' not in line)))
    print('Log:', log)
    return passed, text


def table(name):
    rows = list(csv.DictReader((reports/name).open()))
    groups = {}
    for row in rows:
        groups.setdefault((row['archetype'], row['team']), []).append(row)
    print(f'{name}: {len(rows)} plans')
    for (template, team), group in sorted(groups.items()):
        count = lambda field, value: sum(r[field] == value for r in group)
        worst = max(float(r['total_ms']) for r in group)
        print(f'  {template:20} {team}v{team}  connected pass {count("connected", "pass")}/{len(group)}  exits pass {count("exits", "pass")}/{len(group)}'
              f'  spawn sight pass {count("spawn_los", "pass")}/{len(group)}  worst {worst:.1f}ms')


digest = re.compile(r'ER_MAP_PLAYABILITY_DIGEST (.*)')
if args.batch:
    good, _ = run('batch', 'EclipseRecoil.Maps.Playability.Reports', *(['-ERPlayabilityReports'] if args.reports else []))
    table('layout-summary.csv')
else:
    good, off = run('off', 'EclipseRecoil.Maps.Playability', *(['-ERPlayabilityReports'] if args.reports else []))
    table('layout-summary.csv')
    table('physical-summary.csv')
    # The flag reaches the real map path: same seeds must give the same geometry digests.
    for path in reports.glob('*-physical.json'):
        path.unlink()
    live, on = run('report-only', 'EclipseRecoil.Maps.Playability.Physics', '-ERMapPlayability=ReportOnly')
    good &= live
    a, b = digest.findall(off), digest.findall(on)
    same = bool(a) and a == b
    print(f'report-only neutrality: {len(a)} plans, digests {"identical" if same else "DIFFER"}')
    written = sorted(reports.glob('*-physical.json'))
    hooked = on.count('ER_MAP_PLAYABILITY_LIVE')
    print(f'report-only output: {hooked} live analyses, {len(written)} physical reports')
    good &= same and hooked == len(a) and len(written) == len(a)
    for path in written:
        data = json.loads(path.read_text())
        scenarios = {'Rooms': 2, 'CentralBuilding': 4}.get(data['archetype'], 3)
        unevaluated = sum(metric['status'] == 'not_evaluated' for metric in data['metrics'])
        good &= data['report_version'] == 4 and len(data['metrics']) == 7 and len(data['stress']) == scenarios
        good &= data['unevaluated_metrics'] == unevaluated and data['evaluation_complete'] == (unevaluated == 0)
        good &= not (unevaluated and data['overall'] == 'pass')
raise SystemExit(0 if good else 1)
