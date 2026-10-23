"""Compare native material/layer/batch data with the existing source parser.
This verifies source decoding, not shader interpretation or rendered fidelity.
"""
import argparse,hashlib,json,math,subprocess,sys,tempfile,struct
from pathlib import Path
p=argparse.ArgumentParser();p.add_argument('--exe',type=Path,required=True);p.add_argument('--reference',type=Path,required=True);p.add_argument('--corpus',type=Path,required=True);p.add_argument('--output',type=Path,required=True);a=p.parse_args()
sys.path.insert(0,str(a.reference.resolve()));import io_m3
TYPES=[None,'materials_standard','materials_displacement','materials_composite','materials_terrain','materials_volume','materials_hair','materials_creep','materials_volumenoise','materials_splatterrainbake','materials_reflection','materials_lensflare','materials_buffer']
counts={'materials':0,'layers':0,'batches':0,'properties':0,'compositeSections':0,'bufferPayloads':0}
def records(d,r):return d[r] if r.entries else []
def string(d,r):return d[r].content_to_string() if r.entries else ''
def walk(d,source,prefix=''):
 props={};refs={}
 for name,field in source.desc.fields.items():
  v=getattr(source,name);key=prefix+name
  if isinstance(v,io_m3.M3StructureData):
   if v.desc.history.name in ('Reference','SmallReference'):
    refs[key]=dict(tag=field.ref_to or (d[v].desc.history.name if v.entries else ''),count=v.entries,section=v.index,flags=getattr(v,'flags',0))
   else:
    pp,rr=walk(d,v,key+'.');props.update(pp);refs.update(rr)
  elif not isinstance(v,(bytes,bytearray)):props[key]=v
 return props,refs
def compare(d,native,source):
 assert native['tag']==source.desc.history.name and native['version']==source.desc.version
 assert bytes.fromhex(native['bytes'])==bytes(source.desc.instances_to_bytearray([source]))
 props,refs=walk(d,source)
 assert set(props)=={x['name'] for x in native['properties']}
 for x in native['properties']:
  expected=props[x['name']]
  if isinstance(expected,float):assert math.isclose(x['value'],expected,rel_tol=1e-7,abs_tol=1e-7),(x,expected)
  else:assert x['value']==expected,(x,expected)
 assert refs=={x['name']:{k:v for k,v in x.items() if k!='name'} for x in native['references']}
 counts['properties']+=len(props)
rows=[];documents=[]
for path in sorted(a.corpus.rglob('*')):
 if path.suffix.lower() not in ('.m3','.m3a'):continue
 run=subprocess.run([str(a.exe.resolve()),str(path)],capture_output=True,text=True)
 assert run.returncode==0,(path.name,run.stdout,run.stderr)
 native=json.loads(run.stdout);d=io_m3.M3SectionList.load(str(path),lazy=False);m=d[d[0][0].model][0];matrefs=records(d,m.material_references)
 documents.append((path,d,m))
 assert len(native['materials'])==len(matrefs)
 for got,ref in zip(native['materials'],matrefs):
  source=d[getattr(m,TYPES[ref.type])][ref.material_index]
  assert got['type']==ref.type and got['sourceIndex']==ref.material_index and got['name']==string(d,source.name)
  compare(d,got['record'],source)
  layers=[]
  for name,field in source.desc.fields.items():
   if getattr(field,'ref_to','')!='LAYR':continue
   rr=getattr(source,name)
   for index,layer in enumerate(records(d,rr)):layers.append((name,rr.index,index,layer))
  assert len(got['layers'])==len(layers)
  for actual,(role,section,index,layer) in zip(got['layers'],layers):
   assert (actual['role'],actual['sourceSection'],actual['sourceIndex'],actual['image'])==(role,section,index,string(d,layer.color_bitmap))
   compare(d,actual['record'],layer)
  sections=list(records(d,source.sections)) if ref.type==3 else []
  assert len(got['composite'])==len(sections)
  for actual,section in zip(got['composite'],sections):
   assert actual['materialReference']==section.material_reference_index;compare(d,actual['record'],section)
  textures=[string(d,r.path) for r in records(d,source.texture_paths)] if ref.type==12 else []
  assert got['bufferTextures']==textures
  payloads=[]
  if ref.type==12:
   for role in ['unknown2','unknown3','unknown4']:
    if not hasattr(source,role):continue
    rr=getattr(source,role)
    if rr.entries:
     sec=d[rr];payloads.append(dict(role=role,tag=sec.desc.history.name,version=sec.desc.version,count=rr.entries,bytes=bytes(sec.raw_bytes[:rr.entries*sec.desc.size]).hex()))
  assert got['bufferPayloads']==payloads
  counts['materials']+=1;counts['layers']+=len(layers);counts['compositeSections']+=len(sections);counts['bufferPayloads']+=len(payloads)
 batches=[]
 for divindex,division in enumerate(records(d,m.divisions)):
  for index,b in enumerate(records(d,division.batches)):
   batches.append(dict(division=divindex,index=index,region=b.region_index,material=b.material_reference_index,flags=b.flags,priorityPlane=b.priority_plane,boundsIndex=b.bounds_index,colorIndex=b.color_index,bone=b.bone))
 assert native['batches']==batches;counts['batches']+=len(batches)
 rows.append(dict(path=str(path),sha256=hashlib.sha256(path.read_bytes()).hexdigest(),materials=len(matrefs),layers=sum(len(x['layers']) for x in native['materials']),batches=len(batches),passed=True))

def offset(d,ref,index,field):
 sec=d[ref];where=sec.index_entry.offset+index*sec.desc.size
 for name,f in sec.desc.fields.items():
  if name==field:return where
  where+=f.size
 raise AssertionError(field)
mutations=[]
for path,d,m in documents:
 if m.material_references.entries:
  mutations.append(('material_index',path,offset(d,m.material_references,0,'material_index'),'<I',0xffffffff));break
for path,d,m in documents:
 divisions=list(records(d,m.divisions));div=next((x for x in divisions if x.batches.entries),None)
 if div:
  for field,fmt,value in [('region_index','<H',65535),('material_reference_index','<H',65535),('bone','<h',32767)]:mutations.append((field,path,offset(d,div.batches,0,field),fmt,value))
  break
for path,d,m in documents:
 for index,ref in enumerate(records(d,m.material_references)):
  if ref.type==3:
   comp=d[m.materials_composite][ref.material_index]
   if comp.sections.entries:
    mutations.append(('composite_cycle',path,offset(d,comp.sections,0,'material_reference_index'),'<I',index));break
 else:continue
 break
invalid=[]
with tempfile.TemporaryDirectory() as directory:
 for name,source,address,fmt,value in mutations:
  data=bytearray(source.read_bytes());struct.pack_into(fmt,data,address,value);file=Path(directory)/(name+'.m3');file.write_bytes(data)
  run=subprocess.run([str(a.exe.resolve()),str(file)],capture_output=True,text=True);result=json.loads(run.stdout)
  assert run.returncode==1 and not result['ok'],(name,result)
  invalid.append(dict(case=name,rejected=True,error=result['error']))
a.output.write_text(json.dumps(dict(complete=True,tool=str(a.exe.resolve()),toolSha256=hashlib.sha256(a.exe.read_bytes()).hexdigest(),total=len(rows),counts=counts,files=rows,malformed=invalid,scope='Exact source record bytes, all decoded scalar/reference leaves, layers/texture paths, composite edges/alpha references, buffer payload bytes and draw-batch bindings. No rendered material or buffer graph interpretation claim.'),indent=2))
print(json.dumps({'files':len(rows),**counts,'malformedRejected':len(invalid)}))
