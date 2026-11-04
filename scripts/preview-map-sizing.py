#!/usr/bin/env python3
"""Export actual Unreal layouts and dimensioned PNG/SVG plans for any supported team size.

python3 scripts/preview-map-sizing.py --team-size 2 --seeds 42 73 7919 --verify-seeds 512
python3 scripts/preview-map-sizing.py --team-size 4 --seeds 42 --verify-seeds 32
Requires the compiled Editor module and Pillow. Never writes authored assets.
"""
import argparse
import html
import hashlib
import json
import math
import os
from pathlib import Path
import subprocess
from PIL import Image, ImageDraw, ImageFont

ROOT = Path(__file__).resolve().parents[1]
NAMES = {'CentralBuilding': 'Central Building', 'MiddleEasternAlleys': 'Vicoli',
         'FireTraining': 'Fire Training', 'Rooms': 'Rooms', 'Village': 'Village', 'Town': 'Town'}
NOTES = {'CentralBuilding': 'Edificio centrale: 1 piano + tetto', 'MiddleEasternAlleys': '2 vie, 1 muro centrale, sottopasso',
         'FireTraining': '1 navata lunga e stretta, passerelle locali', 'Rooms': '6 stanze, gallerie e incrocio su 2 quote',
         'Village': '1 blocco, 2 passaggi a gomito + fianchi', 'Town': 'Anello spezzato, corti rientranti e passaggi a L'}
BACKGROUND = '#f7f6f1'
PALETTE = ['#c1b5a3', '#aa8070', '#929b9d', '#ad9065', '#b9aa84', '#717c88', '#b6d8d7']
FONT = '/System/Library/Fonts/Supplemental/Arial.ttf'
BOLD = '/System/Library/Fonts/Supplemental/Arial Bold.ttf'


def hull(points):
    pts = sorted(set((round(p[0], 5), round(p[1], 5)) for p in points))
    if len(pts) < 3:
        return pts
    def cross(o, a, b):
        return (a[0]-o[0])*(b[1]-o[1])-(a[1]-o[1])*(b[0]-o[0])
    lo, hi = [], []
    for p in pts:
        while len(lo) >= 2 and cross(lo[-2], lo[-1], p) <= 0:
            lo.pop()
        lo.append(p)
    for p in reversed(pts):
        while len(hi) >= 2 and cross(hi[-2], hi[-1], p) <= 0:
            hi.pop()
        hi.append(p)
    return lo[:-1] + hi[:-1]


class Drawing:
    """One measured drawing rendered to vector SVG and antialiased PNG."""
    def __init__(self, width, height):
        self.width, self.height = width, height
        self.image = Image.new('RGB', (width*2, height*2), BACKGROUND)
        self.draw = ImageDraw.Draw(self.image)
        self.svg = [f'<svg xmlns="http://www.w3.org/2000/svg" width="{width}" height="{height}" viewBox="0 0 {width} {height}">',
                    f'<rect width="100%" height="100%" fill="{BACKGROUND}"/>']
    def line(self, pts, fill, width=1, dash=False):
        if len(pts) < 2:
            return
        coords = [(round(x*2), round(y*2)) for x, y in pts]
        if dash:
            phase=0.0
            for (x,y),(a,b) in zip(pts,pts[1:]):
                distance=math.hypot(a-x,b-y); at=0.0
                while at<distance-1e-6:
                    length=min(5-phase%5,distance-at)
                    if length<1e-6:
                        phase=(phase+1e-5)%10
                        continue
                    if phase<5:
                        t=at/distance; u=(at+length)/distance
                        self.draw.line([(2*(x+(a-x)*t),2*(y+(b-y)*t)),(2*(x+(a-x)*u),2*(y+(b-y)*u))],fill=fill,width=max(1,round(width*2)))
                    at+=length; phase=(phase+length)%10
        else:
            self.draw.line(coords, fill=fill, width=max(1,round(width*2)))
        attr=' stroke-dasharray="5 5"' if dash else ''
        self.svg.append(f'<polyline points="'+ ' '.join(f'{x:.2f},{y:.2f}' for x,y in pts)+f'" fill="none" stroke="{fill}" stroke-width="{width}"{attr}/>')
    def polygon(self, pts, fill, outline=None, width=1):
        if len(pts) < 3:
            self.line(pts,outline or fill,width)
            return
        self.draw.polygon([(x*2,y*2) for x,y in pts],fill=fill)
        if outline:
            self.draw.line([(x*2,y*2) for x,y in pts+[pts[0]]],fill=outline,width=max(1,round(width*2)))
        self.svg.append('<polygon points="'+' '.join(f'{x:.2f},{y:.2f}' for x,y in pts)+f'" fill="{fill}" stroke="{outline or fill}" stroke-width="{width}"/>')
    def text(self, x, y, text, size=20, color='#243440', bold=False, center=False):
        font=ImageFont.truetype(BOLD if bold else FONT,size*2)
        self.draw.text((x*2,y*2),text,font=font,fill=color,anchor='mt' if center else 'lt')
        self.svg.append(f'<text x="{x}" y="{y+size*.85}" font-family="Arial,sans-serif" font-size="{size}" fill="{color}" font-weight="{"bold" if bold else "normal"}" text-anchor="{"middle" if center else "start"}">{html.escape(text)}</text>')
    def circle(self,x,y,r,fill):
        self.draw.ellipse(((x-r)*2,(y-r)*2,(x+r)*2,(y+r)*2),fill=fill)
        self.svg.append(f'<circle cx="{x}" cy="{y}" r="{r}" fill="{fill}"/>')
    def save(self,path):
        path.with_suffix('.svg').write_text('\n'.join(self.svg+['</svg>']))
        self.image.resize((self.width,self.height),Image.Resampling.LANCZOS).save(path.with_suffix('.png'))


PLAYABILITY=False


def card(d, plan, x, y, w, h, scale):
    ident=plan['template']; tw,th,_=plan['dimensions_m']; n=plan['team_size']
    d.text(x+24,y+14,NAMES[ident],26,bold=True)
    area=plan['area_m2']
    d.text(x+24,y+49,f'{tw:g} × {th:g} m  |  {area:,.0f} m²'.replace(',', ' '),23,bold=True)
    zone_count=len(plan.get("spawn_zones",[]))//2
    zone_note=f" · {zone_count} zone/squadra" if zone_count else ""
    d.text(x+24,y+81,f'{n} vs {n} · seed {plan["seed"]}{zone_note}',17,color='#64717a')
    ox=x+w/2-tw*scale/2; oy=y+135+(h-240-th*scale)/2
    def p(v):
        return (ox+(v[0]+tw/2)*scale,oy+(v[1]+th/2)*scale)
    outline=([p((tw*.5*math.cos(i*math.tau/128),th*.5*math.sin(i*math.tau/128),0)) for i in range(128)]
             if plan.get('circular') else [(ox,oy),(ox+tw*scale,oy),(ox+tw*scale,oy+th*scale),(ox,oy+th*scale)])
    d.polygon(outline,'#e9e8e0','#697a83',1.3)
    profile=plan.get('village_terrain_profile',[])
    if profile:
        def elevation(yy):
            for (a,ha),(b,hb) in zip(profile,profile[1:]):
                if yy<=b:return ha+(hb-ha)*max(0,min(1,(yy-a)/(b-a)))
            return profile[-1][1]
        top=max(k[1] for k in profile)
        for i in range(100):
            ya=-th/2+th*i/100;yb=-th/2+th*(i+1)/100
            t=elevation((ya+yb)/2)/max(top,.01)
            color='#%02x%02x%02x'%(round(235-45*t),round(234-26*t),round(220-44*t))
            d.polygon([p((-tw/2,ya)),p((tw/2,ya)),p((tw/2,yb)),p((-tw/2,yb))],color)
    for at in range(0,math.ceil(tw),5):
        d.line([(ox+at*scale,oy),(ox+at*scale,oy+th*scale)],'#d9ded8',.6)
    for at in range(0,math.ceil(th),5):
        d.line([(ox,oy+at*scale),(ox+tw*scale,oy+at*scale)],'#d9ded8',.6)
    for f in plan['floors']:
        d.polygon([p(v) for v in f], '#f5f1e4','#b3ae9d',.7)
    if plan.get('circular') and not plan.get('central_hill'):
        hx,hy=p(plan['hill_center'])
        d.text(hx,hy-10,f'COLLINA +{plan["hill_height_m"]:.1f} m',12,color='#72894f',bold=True,center=True)
    for deck in plan.get('upper_decks',[]):
        d.polygon([p(v) for v in deck['outline']],'#d8e6eb','#7598aa',1)
    # Ground constructions first; upper footprints use a lighter outline.
    solids=sorted(plan['solids'],key=lambda s:min(v[2] for v in s['corners']))
    for s in solids:
        z0=min(v[2] for v in s['corners']); z1=max(v[2] for v in s['corners'])
        if z1<0:
            continue
        poly=[p(v) for v in hull(s['corners'])]
        if z0>s.get('ground_z_m',0)+1.9:
            if poly and ident!='CentralBuilding':
                d.line(poly+[poly[0]],'#a3b4bd',.65)
        else:
            d.polygon(poly, PALETTE[s['material'] % len(PALETTE)],'#45565c' if ident=='FireTraining' else '#66706d',1 if ident=='FireTraining' else .4)
    for s in plan.get('shell',[]):
        poly=[p(v) for v in s]
        if min(v[2] for v in s)<1.9:
            d.line(poly+[poly[0]],'#697a83',max(1,scale*.15))
    for footprint in plan.get('inaccessible_ground', []):
        d.polygon([p(v) for v in footprint], '#171c20', '#171c20', .8)
    for route in plan['routes']:
        kind=route['kind']
        if kind=='stairs':
            continue
        color='#2b817b' if kind=='connector' else '#8a6aa0' if kind=='lower' else '#4b8c8c' if kind=='upper' else '#6d9462'
        d.line([p(v) for v in route['points']],color,3 if kind=='connector' else 1.8,kind in ('lower','upper'))
        if kind=='connector':
            cx,cy=p((0,0,0));d.text(cx,cy-20,'PASSAGGIO',11,color=color,bold=True,center=True)
    for firing in plan.get('firing_positions',[]):
        d.line([p(v) for v in firing['path']],'#386d91',2)
        fx,fy=p(firing['point']);d.circle(fx,fy,3.5,'#386d91');d.text(fx+7,fy-14,'F',13,color='#386d91',bold=True)
    for room in plan.get('room_details',[]):
        if room['base']:
            continue
        rw,rh,_=room['size_m'];cx,cy=p(room['center'])
        size=max(10,min(16,int(min(rw,rh)*scale/7)))
        d.text(cx,cy+10,f'{rw:g} × {rh:g} m',size,color='#53636b',bold=True,center=True)
    for team in (0,1):
        positions=[s['point'] for s in plan['spawns'] if s['team']==team]
        for v in positions:
            a,b=p(v); d.circle(a,b,2.8,'#217ab6' if team==0 else '#d97732')
        zones=[z for z in plan.get('spawn_zones',[]) if z['team']==team]
        if zones:
            for i,zone in enumerate(zones):
                a,b=p(zone['center']);label=('A' if team==0 else 'O')+str(i+1)
                d.text(a,b-25 if team==0 else b+10,label,13,color='#17629a' if team==0 else '#ae5726',bold=True,center=True)
                d.line([p(v) for v in zone['exit']],'#568b69',1.5)
        elif positions:
            center=[sum(v[k] for v in positions)/len(positions) for k in (0,1)]
            a,b=p(center)
            d.text(a,b-28 if team==0 else b+10,'ALPHA' if team==0 else 'OMEGA',13,color='#17629a' if team==0 else '#ae5726',bold=True,center=True)
    if PLAYABILITY and 'tactical' in plan:
        for edge in plan['tactical']['edges']:
            d.line([p(v) for v in edge],'#3f6fb5',1)
        for sight in plan['tactical']['sight']:
            d.line([p(v) for v in sight],'#c8332b',1.2,True)
        for v in plan['tactical']['chokes']:
            a,b=p(v); d.circle(a,b,5,'#b5258f')
    # Dimension lines are in metres, along the actual arena boundary.
    d.line([(ox,oy-13),(ox+tw*scale,oy-13)],'#60727c',1)
    for a in (ox,ox+tw*scale):
        d.line([(a,oy-18),(a,oy-8)],'#60727c',1)
    d.text(ox+tw*scale/2,oy-35,f'{tw:g} m',15,center=True)
    d.text(ox+tw*scale+8,oy+th*scale/2,f'{th:g} m',15)
    if 'ground_floor_area_m2' in plan:
        floor=plan['ground_floor_area_m2']
        d.text(x+24,y+h-102,f'Pavimento utile {floor:,.0f} m² · {floor/(tw*th)*100:.0f}% della pianta'.replace(',',' '),15,color='#53636b')
    d.text(x+24,y+h-78,f'Obiettivo {n*1500:g} m² ±10% · scarto {(area/(n*1500)-1)*100:+.1f}%',15,color='#53636b')
    note=NOTES[ident] if n<=2 else 'Vincoli e topologia adattati alla squadra'
    if ident=='FireTraining':
        crossing=next((r for r in plan['routes'] if r['kind']=='crossing'),None)
        top=crossing['points'][0][2]-1 if crossing else 0
        note=f"{plan.get('ground_rooms',0)} stanze interne · {plan.get('ground_covers',0)} coperture · soffitto {plan.get('indoor_height_m',0):g} m · rete a +{top:g} m · "+{0:'passerelle a H',1:'ponti sfalsati',2:'anello sopraelevato',3:'doppi gomiti',4:'raccordi sfalsati',5:'passerella a S'}.get(plan.get('catwalk_variant',0),'')
    elif ident=='CentralBuilding':
        if plan.get('circular') and 'building_size_m' in plan:
            bw,bd,_=plan['building_size_m'];floors=plan['building_floors']
            shapes={0:'rettangolare',1:'a H',2:'a croce',3:'a gradoni'}
            rules={1:'aperto',2:'muro scavalcabile 1,2 m',3:'ringhiera con edificio laterale'}
            note=f"{bw:g} × {bd:g} m · {shapes.get(plan['central_shape'],'')} · {floors} "+('piano' if floors==1 else 'piani')+' · '+rules.get(plan['crossing_rule'],'')
            if plan.get('secondary_buildings'):note+=f" · {plan['secondary_buildings']} edifici minori"
            elif plan.get('central_hill'):note+=' · collina centrale'
        else:
            note=('Arena circolare · '+('edificio sulla collina' if plan.get('central_hill') else 'edificio a ovest · collina a est') if plan.get('circular') else 'Arena rettangolare · nucleo centrale e tetto')
    elif ident=='Rooms':
        shapes={'rectangular':'sale rettangolari','compact':'sale compatte','irregular':'sale sagomate'}
        note=(f'4 stanze · {shapes.get(plan.get("hall_shape"),"sale centrali")}' + (' · passaggio centrale' if plan.get('hall_connection') else ' · sale separate') if plan["rooms"]==4 else f'{plan["rooms"]} stanze · '+('gallerie e incrocio su 2 quote' if plan.get('upper_crossing',True) else 'rete di corridoi al piano terra'))
    elif ident=='Village':
        note=f'{plan["blocks"]} '+('blocco' if plan['blocks']==1 else 'blocchi')+f' · {plan.get("storeys",1)} '+('piano' if plan.get("storeys",1)==1 else 'piani')+f' · {plan.get("ground_cover_pockets",0)} ripari a terra'
        if profile:
            center=elevation(0);base=profile[0][1]
            note+=f' · centro +{center:g} m / basi +{base:g} m'
        else:note+=' · terreno piano'

    d.text(x+24,y+h-55,note,16,color='#53636b')
    d.line([(x+25,y+h-24),(x+25+10*scale,y+h-24)],'#324b59',3)
    d.text(x+35+10*scale,y+h-32,'10 m',14)


def render(plans, output):
    output.mkdir(parents=True,exist_ok=True)
    for plan in plans:
        tw,th,_=plan['dimensions_m']
        d=Drawing(1100,1100)
        card(d,plan,0,0,1100,1070,min(880/tw,820/th))
        d.text(25,1065,'Proiezione del generatore reale · ingombri verticali, tetti omessi · griglia 5 m',17,color='#65727a')
        d.save(output/f'{plan["template"]}-seed{plan["seed"]}')
    first_seed=plans[0]['seed']; selected=[p for p in plans if p['seed']==first_seed]
    d=Drawing(1860,1740); n=plans[0]['team_size']
    d.text(32,22,f'PIANTE {n} VS {n}',36,bold=True)
    d.text(32,69,'Stessa scala per tutti i template · dimensioni effettive del perimetro · griglia 5 m',21,color='#64717a')
    scale=min(min(440/p['dimensions_m'][0],390/p['dimensions_m'][1]) for p in selected)
    for i,plan in enumerate(selected):
        card(d,plan,(i%3)*620,122+(i//3)*655,620,635,scale)
    d.text(32,1450,'COME LEGGERE LE PIANTE',21,bold=True)
    for j,(label,color) in enumerate([('Blu / arancio: possibili punti di spawn','#496876'),('Verde: percorsi principali e passaggi','#6d9462'),('Tratteggio viola: sottopasso','#8a6aa0'),('Tratteggio azzurro: percorsi sui tetti','#4b8c8c'),('Contorni chiari: strutture sopraelevate','#8c9ca7'),('Nero: aree inaccessibili escluse dai m²','#171c20')]):
        d.text(32+(j%2)*930,1490+(j//2)*36,label,17,color=color)
    d.text(32,1620,'Piante tecniche, tetti omessi.',18)
    d.text(32,1652,'Le varianti restano da provare in partita.',18)
    d.text(32,1700,f'Generatore Unreal · seed {first_seed} · scala squadra indipendente dal numero di giocatori ammessi',19,color='#64717a')
    d.save(output/f'overview-{n}v{n}')
    for stem,title,identifiers in (
        ('central-fire','CENTRAL BUILDING / FIRE TRAINING',('CentralBuilding','FireTraining')),
        ('rooms-fire','ROOMS / FIRE TRAINING',('Rooms','FireTraining')),
    ):
        focused=[next(p for p in selected if p['template']==ident) for ident in identifiers]
        focus=Drawing(1600,1100)
        focus.text(32,22,f'{n} VS {n} · {title}',32,bold=True)
        focus.text(32,66,'Stessa scala · 750 m² per giocatore previsto · tolleranza ±10% sull’area',21,color='#64717a')
        focus_scale=min(min(620/p['dimensions_m'][0],680/p['dimensions_m'][1]) for p in focused)
        for i,plan in enumerate(focused):
            card(focus,plan,i*800,110,800,950,focus_scale)
        focus.text(32,1071,'Proiezioni native · quote in metri · tetti omessi · blu/arancio: possibili punti di spawn',17,color='#64717a')
        focus.save(output/f'{stem}-{n}v{n}')
    # Compare actual seeds side by side: area/proportions and structural counts
    # should be visible without having to switch between individual SVG tabs.
    seeds=list(dict.fromkeys(p['seed'] for p in plans))[:3]
    varied=[p for ident in ('Rooms','Village') for seed in seeds for p in plans if p['template']==ident and p['seed']==seed]
    comparison=Drawing(620*len(seeds),1510)
    comparison.text(32,22,f'{n} VS {n} · VARIANTI ROOMS / VILLAGE',32,bold=True)
    comparison.text(32,69,'Seed a confronto alla stessa scala · dimensioni in metri · griglia 5 m',21,color='#64717a')
    variety_scale=min(min(440/p['dimensions_m'][0],390/p['dimensions_m'][1]) for p in varied)
    for i,plan in enumerate(varied):
        card(comparison,plan,(i%len(seeds))*620,122+(i//len(seeds))*655,620,635,variety_scale)
    comparison.text(32,1462,'Topologie del generatore reale · 750 m² per giocatore previsto, ±10% sull’area',19,color='#64717a')
    comparison.save(output/f'rooms-village-{n}v{n}')
    room_variants=[p for p in plans if p['template']=='Rooms' and p['rooms']==4][:3]
    if room_variants:
        variants=Drawing(800*len(room_variants),1100)
        variants.text(32,22,f'ROOMS {n} VS {n} · VARIANTI DELLE SALE',32,bold=True)
        variants.text(32,66,'Con o senza passaggio centrale · dimensioni e aree reali · stessa scala',21,color='#64717a')
        variant_scale=min(min(620/p['dimensions_m'][0],680/p['dimensions_m'][1]) for p in room_variants)
        for i,plan in enumerate(room_variants):card(variants,plan,i*800,110,800,950,variant_scale)
        variants.save(output/f'rooms-variants-{n}v{n}')
    cards=[]
    for plan in plans:
        f=f'{plan["template"]}-seed{plan["seed"]}'
        tw,th,_=plan['dimensions_m']
        cards.append(f'<article><h2>{NAMES[plan["template"]]} · {tw:g} × {th:g} m</h2><p>Seed {plan["seed"]} · {n} vs {n} · {plan["area_m2"]:g} m² / obiettivo {n*1500} m² ±10%</p><a href="{f}.svg"><img src="{f}.svg" alt="Pianta quotata"/></a></article>')
    (output/'index.html').write_text(f'<!doctype html><html lang="it"><meta charset="utf-8"><title>Piante {n}vs{n}</title><style>body{{background:{BACKGROUND};color:#243440;font:17px Arial;margin:28px}}main{{display:grid;grid-template-columns:repeat(auto-fit,minmax(440px,1fr));gap:22px}}img{{width:100%}}article{{border:1px solid #cdd2cc;padding:16px}}h1{{margin-bottom:8px}}</style><h1>Piante {n} vs {n}</h1><p>Dimensioni reali in metri. Squadre previste indipendenti dalla capienza. Clicca una pianta per lo SVG.</p><h2>Tutti i template · stessa scala</h2><a href="overview-{n}v{n}.svg"><img src="overview-{n}v{n}.svg" alt="Tutti i template quotati alla stessa scala"/></a><h2>Variabilità Rooms / Village</h2><a href="rooms-village-{n}v{n}.svg"><img src="rooms-village-{n}v{n}.svg" alt="Confronto quotato delle varianti Rooms e Village"/></a><main>'+''.join(cards)+'</main></html>')


def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--team-size',type=int,choices=range(1,17),default=2,metavar='1..16')
    p.add_argument('--seeds',nargs='+',type=int,default=[42,73,7919])
    p.add_argument('--verify-seeds',type=int,default=32,help='Verify seeds 0..N-1 plus signed edge cases (max4096)')
    p.add_argument('--reuse',action='store_true',help='Only redraw a previous native export')
    p.add_argument('--output',type=Path)
    p.add_argument('--playability',action='store_true',help='Overlay tactical graph (blue), chokes (magenta) and spawn sight lines (red)')
    args=p.parse_args()
    global PLAYABILITY
    PLAYABILITY=args.playability
    if not 0<=args.verify_seeds<=4096 or any(not -(2**31)<=s<2**31 for s in args.seeds):
        p.error('Seed validation count or signed32 seed out of range')
    folder=ROOT/'Saved/Testing/MapPlans';folder.mkdir(parents=True,exist_ok=True)
    export=folder/f'plans-{args.team_size}v{args.team_size}.json'
    log=folder/f'generator-{args.team_size}v{args.team_size}.log'
    receipt=folder/f'verified-{args.team_size}v{args.team_size}.json'
    if not args.reuse:
        editor=Path(os.environ.get('UE_ROOT','/Users/Shared/Epic Games/UE_5.8'))/'Engine/Binaries/Mac/UnrealEditor.app/Contents/MacOS/UnrealEditor'
        command=[str(editor),str(ROOT/'RespawnAndRage.uproject'),'/Engine/Maps/Entry','-nullrhi','-nosound','-unattended','-NoSplash','-Multiprocess','-stdout','-FullStdOutLogOutput','-ExecCmds=Automation RunTests EclipseRecoil.Maps.TeamSizing','-TestExit=Automation Test Queue Empty',f'-ERMapPlanTeamSize={args.team_size}','-ERMapPlanSeeds='+','.join(map(str,args.seeds)),f'-ERMapPlanVerifySeeds={args.verify_seeds}']
        before=__import__('time').time()
        with log.open('w') as f:
            result=subprocess.run(command,cwd=ROOT,stdout=f,stderr=subprocess.STDOUT,timeout=1200)
        lines=log.read_text(errors='replace')
        print('\n'.join(s for s in lines.splitlines() if 'ER_MAP_' in s or 'Result={' in s or 'LogAutomationController: Error:' in s))
        if result.returncode or 'Result={Fail}' in lines or 'Result={Success} Name={TeamSizing}' not in lines or not export.exists() or export.stat().st_mtime<before:
            raise SystemExit(f'Native generator failed; see {log}')
    if not args.reuse:
        receipt.write_text(json.dumps({'policy': '750m2-per-player-rooms-floor-v28', 'sha256': hashlib.sha256(export.read_bytes()).hexdigest()}))
    elif not receipt.exists() or json.loads(receipt.read_text()).get('policy') != '750m2-per-player-rooms-floor-v28' or json.loads(receipt.read_text()).get('sha256') != hashlib.sha256(export.read_bytes()).hexdigest():
        raise SystemExit('Native export is not verified; regenerate without --reuse')
    plans=json.loads(export.read_text())
    if not plans or any(q['team_size']!=args.team_size for q in plans) or (not args.reuse and {q['seed'] for q in plans} != set(args.seeds)):
        raise SystemExit('Invalid native export')
    if any(not .9*args.team_size*1500-.01 <= q['area_m2'] <= 1.1*args.team_size*1500+.01 for q in plans):
        raise SystemExit('Native export violates the750m²/player area budget; regenerate')
    output=(args.output or folder/f'{args.team_size}v{args.team_size}').resolve()
    render(plans,output)
    print(output/'index.html')
    print(output/f'overview-{args.team_size}v{args.team_size}.png')

if __name__=='__main__':
    main()
