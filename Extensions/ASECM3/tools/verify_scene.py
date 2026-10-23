"""Source batch/UV/reference parity; clock integration with the separately verified pose tool. CPU evidence only."""
import argparse,hashlib,json,math,subprocess,sys
from pathlib import Path
p=argparse.ArgumentParser()
for name in ['exe','pose','reference','corpus','output']:p.add_argument('--'+name,type=Path,required=True)
a=p.parse_args();sys.path.insert(0,str(a.reference.resolve()));import io_m3
def records(d,r):return list(d[r]) if r.entries else []
def text(d,r):return d[r].content_to_string() if r.entries else ''
def run(exe,*args):
 r=subprocess.run([str(exe.resolve()),*map(str,args)],capture_output=True,text=True)
 assert r.returncode==0,(args,r.stdout,r.stderr)
 return json.loads(r.stdout)
def near(got,want):
 assert len(got)==len(want)
 for x,y in zip(got,want):assert math.isclose(x,y,rel_tol=3e-5,abs_tol=2e-5),(x,y)
rows=[];cases=vertices=uvs=batches=worlds=0
for path in sorted(a.corpus.rglob('*')):
 if path.suffix.lower() not in ('.m3','.m3a'):continue
 d=io_m3.M3SectionList.load(str(path),lazy=False);m=d[d[0][0].model][0]
 seqs=records(d,m.sequences);groups=records(d,m.sequence_transformation_groups);names=[text(d,g.name) for g in groups]
 desc=io_m3.M3StructureDescription.get_vertex_description(m.vertex_flags);source=[]
 if m.vertices.entries:
  raw=d[m.vertices].raw_bytes;source=desc.instances(raw,len(raw)//desc.size)
 expectedgroups=[];samples=[(-1,0,-1,0)]
 for i,s in enumerate(seqs):
  matches=[j for j,n in enumerate(names) if n==text(d,s.name)]
  g=matches[0] if len(matches)==1 else -2 if matches else -1;expectedgroups.append(g)
  if g>=0:
   duration=s.anim_ms_end-s.anim_ms_start
   for elapsed in sorted({0,duration//2,duration,duration+duration//2,2**40}):
    offset=min(elapsed,duration) if s.flags&1 else elapsed%duration if duration else 0
    samples.append((i,elapsed,g,s.anim_ms_start+offset))
 divs=records(d,m.divisions);wantedbatches=[]
 for di,div in enumerate(divs):
  for bi,b in enumerate(records(d,div.batches)):wantedbatches.append(dict(division=di,region=b.region_index,material=b.material_reference_index,index=bi))
 for seq,elapsed,g,t in samples:
  got=run(a.exe,path,seq,elapsed);pose=run(a.pose,path,g,t)
  assert got['sequenceGroups']==expectedgroups and got['batches']==wantedbatches
  assert len(got['world'])==len(pose['world'])
  for x,y in zip(got['world'],pose['world']):near(x,y);worlds+=1
  regions=records(d,divs[0].regions) if divs else []
  faces=list(d[divs[0].faces]) if divs and divs[0].faces.entries else []
  assert len(got['regions'])==len(regions)
  posemap={(r['division'],r['region']):r for r in pose['regions']}
  for ri,(out,region) in enumerate(zip(got['regions'],regions)):
   assert out['division']==0 and out['region']==ri
   assert out['indices']==faces[region.first_face_index:region.first_face_index+region.face_count]
   pp=posemap[(0,ri)];assert len(out['vertices'])==region.vertex_count
   for vi,v in enumerate(out['vertices']):
    near(v['p'],pp['positions'][vi]);near(v['n'],pp['normals'][vi]);vertices+=1
    near(v['tangent'],pp['tangents'][vi]);assert v['sign']==pp['tangentSigns'][vi]
    original=source[region.first_vertex_index+vi];wanted=[]
    assert v['color']==([getattr(original.col,c) for c in 'rgba'] if hasattr(original,'col') else [255]*4)
    for prefix,count,scale in [('fuv',4,1),('uv',5,32768)]:
     for channel in range(count):
      if hasattr(original,prefix+str(channel)):
       uv=getattr(original,prefix+str(channel))
       wanted.extend((float(uv.x)/scale*getattr(region,'uv_multiply',16)+getattr(region,'uv_offset',0),float(uv.y)/scale*getattr(region,'uv_multiply',16)+getattr(region,'uv_offset',0)))
    near(v['uv'],wanted);uvs+=len(wanted)//2
  cases+=1
 paths=[];seen=set()
 types={1:'materials_standard',2:'materials_displacement',3:'materials_composite',4:'materials_terrain',5:'materials_volume',6:'materials_hair',7:'materials_creep',8:'materials_volumenoise',9:'materials_splatterrainbake',10:'materials_reflection',11:'materials_lensflare',12:'materials_buffer'}
 for ref in records(d,m.material_references):
  mat=d[getattr(m,types[ref.type])][ref.material_index];images=[]
  for name,field in mat.desc.fields.items():
   if getattr(field,'ref_to',None)=='LAYR':
    for layer in records(d,getattr(mat,name)):images.append(text(d,layer.color_bitmap))
  if ref.type==12:images.extend(text(d,x.path) for x in records(d,mat.texture_paths))
  for image in images:
   key=image.replace('/','\\').lower()
   if image and key not in seen:seen.add(key);paths.append(image)
 assert got['textures']==paths
 batches+=len(wantedbatches)
 rows.append(dict(path=str(path),sha256=hashlib.sha256(path.read_bytes()).hexdigest(),cases=len(samples),passed=True))
 print(path.name,len(samples),'scene cases',flush=True)
 report=dict(complete=False,tool=str(a.exe.resolve()),toolSha256=hashlib.sha256(a.exe.read_bytes()).hexdigest(),poseToolSha256=hashlib.sha256(a.pose.read_bytes()).hexdigest(),files=rows,cases=cases,vertices=vertices,uvPairs=uvs,batches=batches,boneWorldMatrices=worlds,scope=__doc__)
 a.output.write_text(json.dumps(report,indent=2))
report['complete']=True;a.output.write_text(json.dumps(report,indent=2))
print('PASS',len(rows),'files',cases,'scene cases',vertices,'vertices',uvs,'UV pairs')
