"""Preserve current converter semantics as evidence for the native runtime port.
This emits review candidates. It does not claim automatic bank compatibility.
"""
import argparse,hashlib,json,sys
from pathlib import Path
cli=argparse.ArgumentParser();cli.add_argument('--converter',type=Path,required=True);cli.add_argument('--corpus',type=Path,required=True);cli.add_argument('--output',type=Path,required=True)
a=cli.parse_args();sys.path.insert(0,str((a.converter/'tools').resolve()))
import convert_m3_to_m2 as c
from hots_animation_semantics import classify
corpus=json.loads(a.corpus.read_text())['files']
policies=c.load_animation_overrides(a.converter/'animation_id_overrides.json')
clips=[]
for f in corpus:
 stem=Path(f['path']).stem
 context=stem.removesuffix('_requiredanims')
 policy=c.animation_overrides_for_model(policies,context)
 for index,clip in enumerate(f['animations']):
  name=clip['name'];target,reason,confidence=c.map_m3_animation_name_detailed(name,policy['names'],model_name=context)
  named=policy['policies'].get(c.normalize_animation_name(name),{})
  indexed=policy['indices'].get(index,{})
  effective={**named,**indexed}
  if 'name' in effective and c.normalize_animation_name(effective['name'])!=c.normalize_animation_name(name):raise ValueError('Manual source-index name mismatch')
  if 'animation_id' in effective:target=effective['animation_id'];reason='manual source-index policy' if indexed else 'manual exact-name policy';confidence='manual'
  assert 0<=target<=505
  clips.append(dict(file=f['path'],sourceIndex=index,name=name,start=clip['start'],end=clip['end'],flags=clip['flags'],
   candidateWowId=target,reason=reason,confidence=confidence,policy=effective,semantic=classify(name,context),
   bankRole='facial' if 'facialanims' in stem else 'body_or_effect',
   selectionStatus='review_only_no_runtime_binding'))
def pairs(file,flag=None):
 result=set()
 for b in file['skeleton']:
  if flag and not b['flags']&flag:continue
  for field,kind in [('translationId',2),('rotationId',3),('scaleId',2)]:
   if b[field]:result.add((b[field],kind))
 return result
bindings=[]
for body in corpus:
 if not body['vertices']:continue
 for bank in corpus:
  if Path(bank['path']).suffix.lower()!='.m3a':continue
  bp=pairs(bank);allp=pairs(body);animated=pairs(body,0x200);skinned=pairs(body,0x800)
  bindings.append(dict(model=body['path'],bank=bank['path'],bodyPairs=len(allp),bankPairs=len(bp),matchedPairs=len(allp&bp),
   animatedPairs=len(animated),matchedAnimatedPairs=len(animated&bp),skinnedPairs=len(skinned),matchedSkinnedPairs=len(skinned&bp),
   decision='unproven_requires_dependency_inheritance_and_bind_pose_checks'))
files=[a.converter/'animation_id_overrides.json',a.converter/'hots_animation_contexts.json',a.converter/'tools/hots_animation_semantics.py',a.converter/'tools/convert_m3_to_m2.py']
a.output.write_text(json.dumps(dict(clips=clips,bankCandidates=bindings,sources=[dict(path=str(p),sha256=hashlib.sha256(p.read_bytes()).hexdigest()) for p in files],
 notes=['HotS editor preset IDs and token list positions are not WoW animation IDs.',
 'Manual override precedence is retained, including reviewed source-index names.',
 'Facial bank candidates are not ordinary body idle selections.',
 'The supplied dependency research does not include Alexstrasza model definitions; local source actor data is not a resolved CModel inheritance graph.',
 'These are converter-derived candidate mappings, not a native-runtime semantic parity result.']),indent=2))
print(f'{len(clips)} animation candidates; {len(bindings)} explicit bank compatibility audits')
