"""Independent source sampling/pass-order checks; not pixel/shader validation."""
import hashlib,json,math,subprocess,sys,struct,tempfile
from pathlib import Path
ROOT=Path(__file__).resolve().parents[5];REPO=Path(__file__).resolve().parents[3]
sys.path.insert(0,str(ROOT/'M3_to_M2/tools'));import convert_m3_to_m2 as c
exe=REPO/'build/x64-ninja/bin/ASECM3MaterialEvaluationInspect.exe'
types=[None,'materials_standard','materials_displacement','materials_composite','materials_terrain','materials_volume','materials_hair','materials_creep','materials_volumenoise','materials_splatterrainbake','materials_reflection','materials_lensflare','materials_buffer']
fields=['sdev','sd2v','sd3v','sd4q','sdcc','sdr3']
def arr(d,r):return list(c.section_for(d,r) or [])
def vector(v,t):
 if t==5:return (float(v),)
 return tuple(float(getattr(v,k)) for k in ('xy' if t==1 else 'xyz' if t==2 else 'xyzw' if t==3 else 'rgba'))
def records(d,m):
 result=[]
 for ref in arr(d,m.material_references):result.append(arr(d,getattr(m,types[ref.type]))[ref.material_index])
 return result
cases=values_checked=passes_checked=packets_checked=animated_values=0;rows=[]
for path in sorted(Path(r'G:\Client335\Data\Patch-M.mpq\HOTS_M3').rglob('*.m3')):
 d=c.io_m3.M3SectionList.load(str(path),lazy=False);m=d[d[0][0].model][0];materials=records(d,m)
 samples=[(-1,0)]+[(i,t) for i,g in enumerate(arr(d,m.sequence_transformation_groups)) for t in [0,333,1000,5000]]
 flat={}
 def collect(source,prefix):
  for name,f in source.desc.fields.items():
   v=getattr(source,name)
   if not hasattr(v,'header') or not hasattr(v,'default'):continue
   kind=v.desc.history.name
   t={'Vector2AnimationReference':1,'Vector3AnimationReference':2,'QuaternionAnimationReference':3,'ColorAnimationReference':4,'FloatAnimationReference':5}.get(kind)
   if t:flat[prefix+'.'+name]=(v,t)
 for i,mat in enumerate(materials):
  prefix='m'+str(i);collect(mat,prefix);layer=0
  for name,field in mat.desc.fields.items():
   if getattr(field,'ref_to',None)=='LAYR':
    for value in arr(d,getattr(mat,name)):collect(value,prefix+'.l'+str(layer));layer+=1
  if mat.desc.history.name=='CMP_':
   for j,section in enumerate(arr(d,mat.sections)):collect(section,prefix+'.c'+str(j))
 for group,time in samples:
  tracks={};priorities={}
  if group>=0:
   g=arr(d,m.sequence_transformation_groups)[group]
   for ci in arr(d,g.stc_indices):
    stc=arr(d,m.sequence_transformation_collections)[ci]
    for aid,ref in zip(arr(d,stc.anim_ids),arr(d,stc.anim_refs)):
     t=ref>>16
     if t not in (1,2,3,4,5):continue
     entry=arr(d,getattr(stc,fields[t]))[ref&65535];keys=dict(zip(arr(d,entry.frames),[vector(k,t) for k in arr(d,entry.keys)]));key=(t,aid)
     if key not in priorities or stc.priority>=priorities[key]:
      priorities[key]=stc.priority;tracks[key]=dict(type=t,timestamps=tuple(sorted(keys)),values=tuple(keys[k] for k in sorted(keys)))
  def sample(ref,t):
   default=vector(ref.default,t);track=tracks.get((t,ref.header.id))
   return c._sample_m3_track(track,time,default,ref.header.interpolation,t==3)
  got=json.loads(subprocess.run([str(exe),str(path),str(group),str(time)],capture_output=True,text=True,check=True).stdout)
  assert got['ok'];assert len(got['values'])==len(flat),(path,len(got['values']),len(flat))
  for item in got['values']:
   ref,t=flat[item['path']];expected=sample(ref,t);assert item['type']==t
   assert len(expected)==len(item['value'])
   for a,b in zip(expected,item['value']):assert math.isclose(a,b,rel_tol=3e-5,abs_tol=2e-5),(path,group,time,item,a,b)
   values_checked+=1
   if (t,ref.header.id) in tracks:animated_values+=1
  fixed=[];deferred=[];batch_index=0
  for di,division in enumerate(arr(d,m.divisions)):
   for batch in arr(d,division.batches):
    if di!=0:batch_index+=1;continue
    root=materials[batch.material_reference_index];composite=root.desc.history.name=='CMP_';leaves=[]
    def visit(index,opacity=1):
     mat=materials[index]
     if mat.desc.history.name=='CMP_':
      for section in arr(d,mat.sections):
       alpha=opacity*max(0,min(1,sample(section.alpha_factor,5)[0]))
       if alpha>0:visit(section.material_reference_index,alpha)
     else:
      blend=getattr(mat,'blend_mode',0)
      if blend==0 and opacity<1:blend=1
      leaves.append(dict(batch=batch_index,material=index,opacity=opacity,blend=blend,priority=getattr(root if composite else mat,'priority',0),composite=composite))
    visit(batch.material_reference_index)
    opaque_first=bool(leaves) and leaves[0]['blend']==0
    rank=lambda p:0 if materials[p['material']].desc.history.name=='TER_' else 2 if materials[p['material']].desc.history.name=='CREP' else 1
    for p in sorted(leaves,key=rank):
     (fixed if p['blend']==0 or composite and opaque_first or rank(p)!=1 else deferred).append(p)
    batch_index+=1
  wanted=fixed+sorted(deferred,key=lambda p:-p['priority'])
  assert len(wanted)==len(got['passes'])
  covered=[]
  for packet in got['packets']:
   packets_checked+=1
   indices=list(range(packet['first'],packet['first']+packet['count']))
   assert indices and all(i<len(got['passes']) for i in indices)
   for i in indices:
    p=got['passes'][i]
    assert (p['batch'],p['priority'],p['deferred'])==(packet['batch'],packet['priority'],packet['deferred'])
    assert p['deferred']==(i>=len(fixed))
   covered.extend(indices)
  assert covered==list(range(len(got['passes'])))
  for a,b in zip(wanted,got['passes']):
   for key in a:assert math.isclose(a[key],b[key],rel_tol=3e-5,abs_tol=2e-5),(path,group,time,a,b)
   passes_checked+=1
  cases+=1
 rows.append(dict(path=str(path),sha256=hashlib.sha256(path.read_bytes()).hexdigest(),samples=len(samples)))
# A scratch mutation supplies COL keys absent from the real corpus. It changes
# one single-entry float container into SDCC (same record layout), redirects its
# STC reference, and links an existing color reference to that exact typed ID.
# User files are only read; all writes are restricted to a temporary directory.
source=Path(r'G:\Client335\Data\Patch-M.mpq\HOTS_M3\alexstraszadragon_base_replace_mat\alexstraszadragon_base_replace_mat.m3')
d=c.io_m3.M3SectionList.load(str(source),lazy=False);m=d.model;data=bytearray(source.read_bytes())
def offset(desc,name):
 return sum(field.size for key,field in list(desc.fields.items())[:list(desc.fields).index(name)])
stcs=arr(d,m.sequence_transformation_collections);stc=stcs[1]
assert stc.sdr3.entries==1 and stc.sdcc.entries==0
refs=arr(d,stc.anim_refs);ri=next(i for i,r in enumerate(refs) if r>>16==5)
entry=arr(d,stc.sdr3)[0];assert entry.keys.entries==2 and arr(d,entry.frames)==[0,500]
stc_address=d[m.sequence_transformation_collections].index_entry.offset+d[m.sequence_transformation_collections].desc.size
source_ref=stc_address+offset(stc.desc,'sdr3');target_ref=stc_address+offset(stc.desc,'sdcc')
data[target_ref:target_ref+12]=data[source_ref:source_ref+12];data[source_ref:source_ref+12]=bytes(12)
struct.pack_into('<I',data,d[stc.anim_refs].index_entry.offset+ri*4,4<<16)
index=d[0][0].index_offset
struct.pack_into('<I',data,index+stc.sdr3.index*16,int.from_bytes(b'CCDS','little'))
struct.pack_into('<I',data,index+entry.keys.index*16,int.from_bytes(b'LOC','little'))
key_address=d[entry.keys].index_entry.offset
data[key_address:key_address+8]=bytes([30,20,10,40,90,80,70,100])
mat=arr(d,m.materials_standard)[0];layer=arr(d,mat.layer_diff)[0]
header=d[mat.layer_diff].index_entry.offset+offset(layer.desc,'color_value')+offset(layer.color_value.desc,'header')
struct.pack_into('<HHI',data,header,1,6,arr(d,stc.anim_ids)[ri])
color_fixture=[]
with tempfile.TemporaryDirectory(prefix='asecm3-color-') as temp:
 path=Path(temp)/'color.m3';path.write_bytes(data)
 for time,wanted in [(0,[10,20,30,40]),(250,[40,50,60,70]),(500,[70,80,90,100])]:
  got=json.loads(subprocess.run([str(exe),str(path),'1',str(time)],capture_output=True,text=True,check=True).stdout)
  value=next(item for item in got['values'] if item['path']=='m0.l0.color_value')
  assert value['type']==4 and value['value']==wanted,value
  color_fixture.append(dict(time=time,rgba=value['value']))
composite_fixture=[]
source=Path(r'G:\Client335\Data\Patch-M.mpq\HOTS_M3\alexstrasza_base_deathragdoll\alexstrasza_base_deathragdoll.m3')
d=c.io_m3.M3SectionList.load(str(source),lazy=False);m=d.model;comp=arr(d,m.materials_composite)[0];sections=arr(d,comp.sections)
with tempfile.TemporaryDirectory(prefix='asecm3-composite-') as temp:
 for factors,wanted in [([1,.5,0],[(2,1,0),(3,.5,1)]),([0,.25,1],[(4,1,0),(3,.25,1)])]:
  data=bytearray(source.read_bytes())
  for i,(section,value) in enumerate(zip(sections,factors)):
   address=d[comp.sections].index_entry.offset+i*d[comp.sections].desc.size+offset(section.desc,'alpha_factor')+offset(section.alpha_factor.desc,'default')
   struct.pack_into('<f',data,address,value)
  path=Path(temp)/'composite.m3';path.write_bytes(data)
  got=json.loads(subprocess.run([str(exe),str(path),'-1','0'],capture_output=True,text=True,check=True).stdout)
  actual=[(p['material'],p['opacity'],p['blend']) for p in got['passes']]
  assert actual==wanted,(factors,actual,wanted)
  composite_fixture.append(dict(factors=factors,passes=actual))
report=dict(ok=True,files=rows,cases=cases,values=values_checked,animatedValues=animated_values,passes=passes_checked,packets=packets_checked,colorFixture=color_fixture,compositeFixture=composite_fixture,executableSHA256=hashlib.sha256(exe.read_bytes()).hexdigest(),scope='Source sampling/pass order, packet coverage/deferred classification, synthetic COL interpolation and composite opacity/order; no native queue or rendered shader/pixel proof.')
(REPO.parent/'verification/m3/material-evaluation-parity.json').write_text(json.dumps(report,indent=2)+'\n',encoding='utf-8')
print(json.dumps({k:v for k,v in report.items() if k!='files'},indent=2))
