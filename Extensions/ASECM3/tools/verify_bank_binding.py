"""Check external M3A deformation transfer against independent source matrix/skin math."""
import json,sys,subprocess,hashlib,math
from pathlib import Path
ROOT=Path(__file__).resolve().parents[5];sys.path.insert(0,str(ROOT/'M3_to_M2/tools'));import convert_m3_to_m2 as c
repo=Path(__file__).resolve().parents[3];exe=repo/'build/x64-ninja/bin/ASECM3SceneInspect.exe';corpus=Path(r'G:\Client335\Data\Patch-M.mpq\HOTS_M3');out=repo.parent/'verification/m3/bank-binding-parity.json'
def load(p):
 d=c.io_m3.M3SectionList.load(str(p),lazy=False);return d,d[d[0][0].model][0]
def arr(d,r):return list(d[r]) if r.entries else []
def near(a,b):
 assert len(a)==len(b)
 for x,y in zip(a,b):assert math.isclose(x,y,rel_tol=4e-5,abs_tol=3e-5),(x,y)
def source_skins(d,m,group,time):
 bones=arr(d,m.bones);rests=arr(d,m.bone_rests);tracks={};priorities={};groups=arr(d,m.sequence_transformation_groups);collections=arr(d,m.sequence_transformation_collections)
 for ci in arr(d,groups[group].stc_indices):
  stc=collections[ci]
  for key,value in c._read_stc_tracks(d,stc,'native_bank_validation',[]).items():
   if key not in priorities or stc.priority>=priorities[key]:tracks[key]=value;priorities[key]=stc.priority
 worlds={}
 def world(i):
  if i in worlds:return worlds[i]
  b=bones[i]
  def sample(ref,kind,default):
   t=tracks.get(ref.header.id)
   if t is not None and t['type']!=kind:t=None
   return c._sample_m3_track(t,time,default,ref.header.interpolation,kind==3)
  local=c.compose_transform(sample(b.location,2,c._vec3(b.location.default)),sample(b.rotation,3,c._quat(b.rotation.default)),sample(b.scale,2,c._vec3(b.scale.default)))
  worlds[i]=c.multiply_matrices(world(b.parent),local) if b.parent>=0 else local;return worlds[i]
 return [c.multiply_matrices(world(i),c.m3_matrix_rows(rests[i].matrix)) for i in range(len(bones))]
rows=[];cases=vertices=0
for stem,family in [('alexstrasza_base','alexstrasza'),('alexstrasza_dark','alexstrasza'),('alexstrasza_spooky18','alexstrasza_spooky18'),('alexstraszadragon_base','alexstraszadragon')]:
 path=corpus/stem/(stem+'.m3');bank=corpus/(family+'_requiredanims')/(family+'_requiredanims.m3a');d,m=load(path);bd,bm=load(bank);bones=arr(d,m.bones);bankbones=arr(bd,bm.bones);rests=arr(d,m.bone_rests);palette=arr(d,m.bone_lookup);seqs=arr(bd,bm.sequences)
 desc=c.io_m3.M3StructureDescription.get_vertex_description(m.vertex_flags);raw=d[m.vertices].raw_bytes;source=desc.instances(raw,len(raw)//desc.size)
 indices={0}
 for i,s in enumerate(seqs):
  if c.section_string(bd,s.name) in ('Stand','Walk','Walk A','Stand 01'):indices.add(i)
 for index in sorted(indices):
  seq=seqs[index];duration=seq.anim_ms_end-seq.anim_ms_start
  for elapsed in [0,500,1500]:
   r=subprocess.run([str(exe),str(path),str(index),str(elapsed),str(bank)],capture_output=True,text=True);assert r.returncode==0,r.stdout;got=json.loads(r.stdout)
   assert got['ok'] and got['weightCoverage']>=.9
   group=got['sequenceGroups'][index];t=seq.anim_ms_start+(min(elapsed,duration) if seq.flags&1 else elapsed%duration if duration else 0)
   skin=source_skins(bd,bm,group,t);mapping=got['meshToBank'];meshskin=[]
   for i,bi in enumerate(mapping):
    if bi>=0:
     assert c.section_string(d,bones[i].name)==c.section_string(bd,bankbones[bi].name)
     for prop in ['location','rotation','scale']:assert getattr(bones[i],prop).header.id==getattr(bankbones[bi],prop).header.id
    p=i
    while p>=0 and mapping[p]<0:p=bones[p].parent
    deform=skin[mapping[p]] if p>=0 else c.IDENTITY_MATRIX;meshskin.append(deform)
    world=c.multiply_matrices(deform,c.inverse_affine(c.m3_matrix_rows(rests[i].matrix)))
    near(got['world'][i],[world[r][col] for col in range(4) for r in range(4)])
   div=arr(d,m.divisions)[0]
   for ri,region in enumerate(arr(d,div.regions)):
    target=got['regions'][ri]
    for vi,v in enumerate(target['vertices']):
     src=source[region.first_vertex_index+vi];weights=[getattr(src,'weight'+str(j),0) for j in range(4)];total=sum(weights);p=c._vec3(src.pos);expected=[0.,0.,0.] if total else p
     for j,w in enumerate(weights):
      if not w:continue
      bi=palette[region.first_bone_lookup_index+getattr(src,'lookup'+str(j))];point=c.transform_point(meshskin[bi],p)
      for axis in range(3):expected[axis]+=w/total*point[axis]
     near(v['p'],expected);vertices+=1
   cases+=1
 rows.append(dict(model=str(path),bank=str(bank),meshSha256=hashlib.sha256(path.read_bytes()).hexdigest(),bankSha256=hashlib.sha256(bank.read_bytes()).hexdigest(),clips=len(seqs),matchedBones=got['matchedBones'],animatedBones=got['animatedBones'],weightCoverage=got['weightCoverage'],passed=True));print(stem,'PASS',len(seqs),'clips',flush=True)
rejected=[]
for stem,family in [('alexstrasza_base','alexstraszadragon'),('alexstraszadragon_base','alexstrasza')]:
 r=subprocess.run([str(exe),str(corpus/stem/(stem+'.m3')),'0','500',str(corpus/(family+'_requiredanims')/(family+'_requiredanims.m3a'))],capture_output=True,text=True);d=json.loads(r.stdout);assert r.returncode==1 and not d['ok'];rejected.append(dict(mesh=stem,bank=family,error=d['error']))
out.write_text(json.dumps(dict(complete=True,tool=str(exe),toolSha256=hashlib.sha256(exe.read_bytes()).hexdigest(),files=rows,cases=cases,vertices=vertices,rejected=rejected,scope=__doc__),indent=2));print('PASS',cases,'cases',vertices,'vertices; incompatible banks rejected')
