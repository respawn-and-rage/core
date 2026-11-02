#!/usr/bin/env python3
"""Candidate selection and destruction stress: rules, batch, real collision, real breaches."""
import argparse
import csv
import os
from pathlib import Path
import subprocess

p = argparse.ArgumentParser(description=__doc__)
p.add_argument('--batch', action='store_true', help='20 seeds per template and size')
p.add_argument('--physics', action='store_true', help='Real collision, with and without selection, on the same seeds')
p.add_argument('--teams', default='2,4,8', help='Expected team sizes')
p.add_argument('--seeds', default='42,73,7919', help='Seeds, ignored by --batch')
p.add_argument('--candidates', type=int, default=0, help='Force a candidate count, for calibration')
p.add_argument('--stress-teams', default='4', help='Team sizes for the real stress scenarios')
args = p.parse_args()
root = Path(__file__).resolve().parents[1]
out = root/'Saved/Testing'
reports = out/'MapPlayability'
reports.mkdir(parents=True, exist_ok=True)
exe = Path(os.environ.get('UE_ROOT', '/Users/Shared/Epic Games/UE_5.8'))/'Engine/Binaries/Mac/UnrealEditor.app/Contents/MacOS/UnrealEditor'
seeds = ','.join(str(s) for s in range(20)) if args.batch else args.seeds
tests = ['EclipseRecoil.Maps.Selection.Decide', 'EclipseRecoil.Maps.Selection.Routes', 'EclipseRecoil.Maps.Selection.Batch', 'EclipseRecoil.Maps.Stress.Rules', 'EclipseRecoil.Maps.Stress.FreshSupport', 'EclipseRecoil.Maps.Stress.Physics']
if args.physics:
    tests.append('EclipseRecoil.Maps.Selection.Physics')
log = out/'map-selection.log'
with log.open('w') as f:
    result = subprocess.run([str(exe), str(root/'RespawnAndRage.uproject'), '/Engine/Maps/Entry', '-nullrhi', '-nosound', '-unattended', '-NoSplash', '-Multiprocess', '-stdout', '-FullStdOutLogOutput',
        '-ExecCmds=Automation RunTests ' + '+'.join(tests), '-TestExit=Automation Test Queue Empty', f'-ERPlayabilityTeams={args.teams}', f'-ERStressTeams={args.stress_teams}', f'-ERPlayabilitySeeds={seeds}',
        f'-ERSelectionCandidates={args.candidates}'], stdout=f, stderr=subprocess.STDOUT, cwd=root)
text = log.read_text(errors='replace')
print('\n'.join(line for line in text.splitlines() if 'Result={' in line))
errors = [line for line in text.splitlines() if 'Error:' in line and 'Condition failed' not in line]
print('\n'.join(errors[:20]))
print('Log:', log)
assert result.returncode == 0 and 'Result={Success}' in text and 'Result={Fail}' not in text, 'native tests failed'

rows = list(csv.DictReader((reports/'selection-candidates.csv').open()))
groups = {}
for row in rows:
    groups.setdefault((row['archetype'], row['team']), []).append(row)
print(f'candidates: {len(rows)}')
for (template, team), group in sorted(groups.items()):
    plans = {r['seed'] for r in group}
    chosen = [r for r in group if r['chosen'] == '1']
    moved = sum(r['index'] != '0' for r in chosen)
    fallback = sum(r['fallback'] == '1' for r in chosen)
    worst = max(sum(float(r['build_ms']) + float(r['analysis_ms']) for r in group if r['seed'] == seed) for seed in plans)
    print(f'  {template:20} {team}v{team}  seeds {len(plans)}  moved {moved}  fallback {fallback}  worst {worst:.0f}ms')
stress = list(csv.DictReader((reports/'stress-summary.csv').open()))
scenarios = {}
for row in stress:
    scenarios.setdefault((row['archetype'], row['scenario']), []).append(row)
print('stress:')
for (template, scenario), group in sorted(scenarios.items()):
    live = [r for r in group if r['applicable'] == '1']
    sight = sum(int(r['visible_pairs_damaged']) > int(r['visible_pairs']) for r in live)
    paths = sum(int(r['paths_damaged']) < int(r['paths']) for r in live)
    exits = sum(float(r['exit_share_damaged']) > float(r['exit_share']) for r in live)
    print(f'  {template:20} {scenario:15} applicable {len(live)}/{len(group)}  new spawn sight {sight}  fewer paths {paths}  wider exit view {exits}')
real = list(csv.DictReader((reports/'stress-physical.csv').open()))
scenarios = {}
for row in real:
    scenarios.setdefault((row['archetype'], row['selected'], row['scenario']), []).append(row)
print('real stress (selected 0: plain seed, 1: chosen plan where it differs):')
for (template, selected, scenario), group in sorted(scenarios.items()):
    live = [r for r in group if r['applicable'] == '1']
    if not live:
        print(f'  {template:20} {selected} {scenario:15} applicable 0/{len(group)}')
        continue
    more = lambda name: sum(float(r[name+'_damaged']) > float(r[name]) for r in live)
    fewer = sum(float(r['real_paths_damaged']) < float(r['real_paths']) for r in live)
    beyond = sum(float(r['real_pairs_damaged']) > float(r['abstract_pairs_damaged']) for r in live)
    collapsed = sum(r['cells_detached'] != '0' for r in live)
    print(f'  {template:20} {selected} {scenario:15} applicable {len(live)}/{len(group)}  sight abstract {more("abstract_pairs")} real {more("real_pairs")}  open shots {more("real_shots")}  spawn seen {more("spawn_seen")}  fewer paths {fewer}  collapse {collapsed}  beyond abstract {beyond}')
loose = {}
for row in csv.DictReader((reports/'fresh-support.csv').open()):
    plans = loose.setdefault(row['archetype'], {})
    key = row['team'], row['seed']
    plans[key] = plans.get(key, 0)+int(row['cells'])
print('cells no anchor holds on a fresh map, which fall at the first damage anywhere:')
for template, plans in sorted(loose.items()):
    print(f'  {template:20} {len(plans)}/{len(seeds.split(","))*len(args.teams.split(","))} plans, up to {max(plans.values())} cells')
print('Map selection: PASS')
