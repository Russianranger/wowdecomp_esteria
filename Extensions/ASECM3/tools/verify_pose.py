"""Native pose/CPU-skin comparisons against independent Python source reads/math.
No visual or bank-retarget correctness claim is made by this verifier.
"""
import argparse,json,math,subprocess,sys,hashlib
from pathlib import Path
p=argparse.ArgumentParser();p.add_argument('--exe',type=Path,required=True);p.add_argument('--converter',type=Path,required=True);p.add_argument('--corpus',type=Path,required=True);p.add_argument('--output',type=Path,required=True);a=p.parse_args()
sys.path.insert(0,str((a.converter/'tools').resolve()));import convert_m3_to_m2 as c

def get(doc,ref):return list(c.section_for(doc,ref) or [])
def matrixflat(m):return [m[r][col] for col in range(4) for r in range(4)]
def near(got,expected,label):
 assert len(got)==len(expected),(label,len(got),len(expected))
 error=0
 for x,y in zip(got,expected):
  assert math.isclose(x,y,rel_tol=3e-5,abs_tol=2e-5),(label,x,y)
  error=max(error,abs(x-y))
 return error
rows=[];cases=0;vertices=0;maximum=0
for path in sorted(a.corpus.rglob('*')):
 if path.suffix.lower() not in ('.m3','.m3a'):continue
 doc=c.io_m3.M3SectionList.load(str(path));model=doc[doc[0][0].model][0]
 bones=get(doc,model.bones);rests=get(doc,model.bone_rests);groups=get(doc,model.sequence_transformation_groups);collections=get(doc,model.sequence_transformation_collections)
 sequences=get(doc,model.sequences);palette=get(doc,model.bone_lookup)
 source_vertices=[]
 if model.vertices.entries:
  raw=doc[model.vertices].raw_bytes;desc=c.io_m3.M3StructureDescription.get_vertex_description(model.vertex_flags);source_vertices=desc.instances(raw,len(raw)//desc.size)
 samples=[(-1,0)]
 for index,seq in enumerate(sequences):
  if index<len(groups):samples.extend((index,time) for time in sorted({int(seq.anim_ms_start),(int(seq.anim_ms_start)+int(seq.anim_ms_end))//2,int(seq.anim_ms_end)}))
 for group,time in samples:
  result=subprocess.run([str(a.exe.resolve()),str(path),str(group),str(time)],capture_output=True,text=True)
  native=json.loads(result.stdout);assert result.returncode==0 and native['ok'],(path.name,group,time,native)
  tracks={};priorities={}
  if group>=0:
   for ci in get(doc,groups[group].stc_indices):
    stc=collections[ci]
    for key,value in c._read_stc_tracks(doc,stc,'native_pose_comparison',[]).items():
     if key not in priorities or stc.priority>=priorities[key]:tracks[key]=value;priorities[key]=stc.priority
  worlds={}
  def world(index):
   if index in worlds:return worlds[index]
   bone=bones[index];assert bone.flags&7==0
   def sample(ref,kind,default):
    track=tracks.get(ref.header.id)
    if track is not None and track['type']!=kind:track=None
    return c._sample_m3_track(track,time,default,ref.header.interpolation,kind==3)
   local=c.compose_transform(sample(bone.location,2,c._vec3(bone.location.default)),sample(bone.rotation,3,c._quat(bone.rotation.default)),sample(bone.scale,2,c._vec3(bone.scale.default)))
   worlds[index]=c.multiply_matrices(world(bone.parent),local) if bone.parent>=0 else local
   return worlds[index]
  skins=[];normals=[]
  for i,bone in enumerate(bones):
   w=world(i);skin=c.multiply_matrices(w,c.m3_matrix_rows(rests[i].matrix));skins.append(skin)
   try:
    inv=c.inverse_affine(skin);normal=tuple(tuple(inv[col][r] if r<3 and col<3 else 0 for col in range(4)) for r in range(4))
   except ValueError:normal=((0,0,0,0),)*4
   normals.append(normal)
   maximum=max(maximum,near(native['world'][i],matrixflat(w),'world'),near(native['skin'][i],matrixflat(skin),'skin'),near(native['normal'][i],matrixflat(normal),'normal'))
  output_index=0
  for di,division in enumerate(get(doc,model.divisions)):
   for ri,region in enumerate(get(doc,division.regions)):
    output=native['regions'][output_index];output_index+=1;assert output['division']==di and output['region']==ri
    for vi in range(region.vertex_count):
     vertex=source_vertices[region.first_vertex_index+vi];weights=[getattr(vertex,'weight'+str(i),0) for i in range(4)];total=sum(weights)
     position=c._vec3(vertex.pos)
     normal=tuple(x/127.5-1 for x in c._vec3(vertex.normal)) if hasattr(vertex,'normal') else c._vec3(vertex.normalf) if hasattr(vertex,'normalf') else (0,0,0)
     tangent=tuple(x/127.5-1 for x in c._vec3(vertex.tan)) if hasattr(vertex,'tan') else c._vec3(vertex.tanf) if hasattr(vertex,'tanf') else (0,0,0)
     source_sign=-1 if getattr(vertex,'sign',255)<128 else 1
     bitangent=[source_sign*(normal[1]*tangent[2]-normal[2]*tangent[1]),source_sign*(normal[2]*tangent[0]-normal[0]*tangent[2]),source_sign*(normal[0]*tangent[1]-normal[1]*tangent[0])]
     if total:
      position_out=[0.,0.,0.];normal_out=[0.,0.,0.];tangent_out=[0.,0.,0.];bitangent_out=[0.,0.,0.]
      for j,weight in enumerate(weights):
       if not weight:continue
       bi=palette[region.first_bone_lookup_index+getattr(vertex,'lookup'+str(j))]
       point=c.transform_point(skins[bi],position)
       direction=[sum(normals[bi][r][k]*normal[k] for k in range(3)) for r in range(3)]
       for axis in range(3):position_out[axis]+=weight/total*point[axis];normal_out[axis]+=weight/total*direction[axis]
       for axis in range(3):
        tangent_out[axis]+=weight/total*sum(skins[bi][axis][k]*tangent[k] for k in range(3))
        bitangent_out[axis]+=weight/total*sum(skins[bi][axis][k]*bitangent[k] for k in range(3))
      position,normal=position_out,normal_out
      tangent,bitangent=tangent_out,bitangent_out
     length=math.sqrt(sum(x*x for x in normal));normal=[x/length for x in normal] if length>1e-12 else normal
     dot=sum(a*b for a,b in zip(normal,tangent));tangent=[tangent[i]-dot*normal[i] for i in range(3)];tl=math.sqrt(sum(x*x for x in tangent));sign=0
     if (hasattr(vertex,'tan') or hasattr(vertex,'tanf')) and tl>1e-12 and length>1e-12:
      tangent=[x/tl for x in tangent];cross=[normal[1]*tangent[2]-normal[2]*tangent[1],normal[2]*tangent[0]-normal[0]*tangent[2],normal[0]*tangent[1]-normal[1]*tangent[0]];handed=sum(a*b for a,b in zip(cross,bitangent))
      if getattr(vertex,'sign',None) in (0,255) and abs(handed)>1e-12:sign=-1 if handed<0 else 1
     else:tangent=[0,0,0]
     maximum=max(maximum,near(output['tangents'][vi],tangent,'skinned tangent'));assert output['tangentSigns'][vi]==sign
     maximum=max(maximum,near(output['positions'][vi],position,'skinned position'),near(output['normals'][vi],normal,'skinned normal'));vertices+=1
  cases+=1
 rows.append(dict(path=str(path),sha256=hashlib.sha256(path.read_bytes()).hexdigest(),poseCases=len(samples),passed=True))
 a.output.write_text(json.dumps(dict(complete=False,tool=str(a.exe.resolve()),toolSha256=hashlib.sha256(a.exe.read_bytes()).hexdigest(),files=rows,cases=cases,skinnedVerticesCompared=vertices,maximumAbsoluteError=maximum,scope='Same-document default/clip source times; priority merge; world/skin/normal matrices and all region vertices. No external-bank retarget, selective inheritance, runtime global clock, materials or visual verification.'),indent=2))
 print(path.name,len(samples),'poses')
report=json.loads(a.output.read_text());report['complete']=True;a.output.write_text(json.dumps(report,indent=2))
print('PASS',len(rows),'files',cases,'pose cases',vertices,'skinned vertices','max error',maximum)
