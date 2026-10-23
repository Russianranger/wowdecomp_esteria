"""Compare native action metadata with the existing source-backed converter.

This validates classification/policy only, not animation playback or visual fit.
"""
import hashlib
import json
from pathlib import Path
import subprocess
import sys

ROOT=Path(__file__).resolve().parents[5]
REPO=Path(__file__).resolve().parents[3]
sys.path.insert(0,str(ROOT/'M3_to_M2/tools'))
import convert_m3_to_m2 as c
import hots_animation_semantics as h

exe=REPO/'build/x64-ninja/bin/ASECM3SemanticsInspect.exe'
policy=c.load_animation_overrides(ROOT/'M3_to_M2/animation_id_overrides.json')
cases=[]
for path in sorted(Path(r'G:\Client335\Data\Patch-M.mpq\HOTS_M3').rglob('*')):
    if path.suffix.lower() not in ('.m3','.m3a'):continue
    doc=c.io_m3.M3SectionList.load(str(path),lazy=False);model=doc[doc[0][0].model][0]
    for i,clip in enumerate(list(doc[model.sequences]) if model.sequences.entries else []):
        cases.append((path.stem,i,int(clip.flags),c.section_string(doc,clip.name)))
# Context queries cover semantics absent from this Alexstrasza-heavy corpus.
for hero,data in h.contexts()['heroes'].items():
    for actor in data['actors']:
        if actor['actor'].casefold()!='hero'+hero:continue
        for query in actor['queries']:cases.append((hero+'_base',999,0,' '.join(query['tokens'])))
for name in ['Walk A Cover','Stand Cover','Walk A Ridesurf Start','Spell A Hover','Spell Omni Channel','Stand Ready Fly','StandCover','SpellCast','WalkFast','GLstand','Portrait Stand','Standup','Stand Talk','Talk Dance','Unknown Eyes','Run Swim Backwards']:
    for hero in ['valeera_base','unknown_base']:cases.append((hero,999,0,name))
cases.extend([('whitemane_base',0,0,'Walk A'),('whitemane_base',1,0,'Stand')])
data=''.join(f'{model}\t{index}\t{flags}\t{name}\n' for model,index,flags,name in cases)
proc=subprocess.run([str(exe)],input=data,text=True,capture_output=True,check=True)
got=[json.loads(line) for line in proc.stdout.splitlines()]
if len(got)!=len(cases):raise RuntimeError('Diagnostic output row count mismatch')
failures=[];guarded=0
for (model,index,flags,name),actual in zip(cases,got):
    resolved=c.animation_overrides_for_model(policy,model)
    p={**resolved['policies'].get(c.normalize_animation_name(name),{}),**resolved['indices'].get(index,{})}
    expected_name=p.get('name')
    if expected_name and c.normalize_animation_name(expected_name)!=c.normalize_animation_name(name):
        guarded+=1
        if actual['ok']:failures.append(dict(model=model,index=index,name=name,error='Stale index guard did not reject'))
        continue
    action,reason,confidence=c.map_m3_animation_name_detailed(name,resolved['names'],model_name=model)
    expected=dict(action=p.get('animation_id',action),excluded=p.get('exclude',False),
                  global_=bool(flags&4) or p.get('global_sequence',False),weight=p.get('frequency_weight',1))
    expected['global']=expected.pop('global_')
    if not actual['ok'] or any(actual[k]!=v for k,v in expected.items()):
        failures.append(dict(model=model,index=index,name=name,expected=expected,actual=actual))
report=dict(ok=not failures,cases=len(cases),rejectedStaleIndices=guarded,failures=failures,
            executableSHA256=hashlib.sha256(exe.read_bytes()).hexdigest(),
            scope='Source corpus and exact body actor queries; IDs/exclusion/global/weight parity. No visual/runtime or concurrent-layer proof.')
out=REPO.parent/'verification/m3/semantics-parity.json'
out.write_text(json.dumps(report,indent=2)+'\n',encoding='utf-8')
print(json.dumps(report,indent=2))
sys.exit(0 if report['ok'] else 1)
