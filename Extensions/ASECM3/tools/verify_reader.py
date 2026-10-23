"""Compare native reads with the established workspace parser on actual inputs.
The reference parser is a verification dependency, not a runtime dependency.
"""
import argparse, hashlib, json, math, subprocess, sys, tempfile
from pathlib import Path
cli=argparse.ArgumentParser()
cli.add_argument('--exe',type=Path,required=True)
cli.add_argument('--reference',type=Path,required=True)
cli.add_argument('--corpus',type=Path,required=True)
cli.add_argument('--output',type=Path,required=True)
args=cli.parse_args()
sys.path.insert(0,str(args.reference.resolve()))
import io_m3

def string(doc,ref): return doc[ref].content_to_string() if ref.entries else ''
def array(doc,ref): return doc[ref] if ref.entries else []
def close(a,b):
    assert len(a)==len(b),(len(a),len(b))
    for x,y in zip(a,b): assert math.isclose(x,y,rel_tol=1e-5,abs_tol=1e-6),(x,y)
def vector(v): return [v.x,v.y,v.z]
rows=[]
for path in sorted(args.corpus.rglob('*')):
    if path.suffix.lower() not in ('.m3','.m3a'): continue
    result=subprocess.run([str(args.exe.resolve()),str(path),'--geometry'],capture_output=True,text=True,check=True)
    native=json.loads(result.stdout)['files'][0]
    doc=io_m3.M3SectionList.load(str(path), lazy=False)
    model=doc[doc[0][0].model][0]
    assert native['model']==string(doc,model.model_name)
    desc=io_m3.M3StructureDescription.get_vertex_description(model.vertex_flags)
    vertices=[]
    if model.vertices.entries:
        raw=doc[model.vertices].raw_bytes
        vertices=desc.instances(raw,len(raw)//desc.size)
    assert native['vertices']==len(vertices)
    for actual,expected in zip(native['vertexData'],vertices):
        close(actual['position'],vector(expected.pos))
        if hasattr(expected,'normal'):
            close(actual['normal'],[(x/127.5)-1 for x in vector(expected.normal)])
        elif hasattr(expected,'normalf'): close(actual['normal'],vector(expected.normalf))
        for key,prefix in [('weights','weight'),('lookup','lookup')]:
            wanted=[getattr(expected,prefix+str(i),0) for i in range(4)]
            assert actual[key]==wanted,(key,actual[key],wanted)
        uv=[]
        for i in range(4):
            if hasattr(expected,'fuv'+str(i)):
                v=getattr(expected,'fuv'+str(i));uv.extend((v.x,v.y))
        for i in range(5):
            if hasattr(expected,'uv'+str(i)):
                v=getattr(expected,'uv'+str(i));uv.extend((v.x/32768,v.y/32768))
        close(actual['uv'],uv)
        tangent=vector(expected.tanf) if hasattr(expected,'tanf') else [0,0,0]
        if hasattr(expected,'tan'):tangent=[x/127.5-1 for x in vector(expected.tan)]
        close(actual['tangent'],tangent)
        assert actual['hasTangent']==(hasattr(expected,'tan') or hasattr(expected,'tanf'))
        assert actual['tangentSign']==getattr(expected,'sign',255)
        assert actual['hasColor']==hasattr(expected,'col')
        assert actual['color']==([getattr(expected.col,k) for k in 'rgba'] if hasattr(expected,'col') else [255]*4)
    skeleton=[]
    for b in array(doc,model.bones):
        skeleton.append(dict(name=string(doc,b.name),parent=b.parent,flags=b.flags,
            translationId=b.location.header.id,rotationId=b.rotation.header.id,scaleId=b.scale.header.id))
    assert native['skeleton']==skeleton
    animations=[dict(name=string(doc,a.name),start=a.anim_ms_start,end=a.anim_ms_end,flags=a.flags) for a in array(doc,model.sequences)]
    assert native['animations']==animations
    regions=[]
    for division in array(doc,model.divisions):
        for region in array(doc,division.regions): regions.append(dict(vertices=region.vertex_count,indices=region.face_count))
    assert native['regions']==regions
    assert not native['unknownSections'],native['unknownSections']
    rows.append(dict(path=str(path),sha256=hashlib.sha256(path.read_bytes()).hexdigest(),vertices=len(vertices),bones=len(skeleton),animations=len(animations),passed=True))
# Malformed versions of a real model exercise error returns, not successful
# parsing of unrelated synthetic files.
source=next(p for p in args.corpus.rglob('*.m3'))
original=source.read_bytes()
mutations={'empty':b'', 'header_only':original[:24], 'truncated_index':original[:-1],
           'bad_magic':b'NOPE'+original[4:],
           'bad_index_offset':original[:4]+(0xffffffff).to_bytes(4,'little')+original[8:],
           'bad_model_section':original[:16]+(0xffffffff).to_bytes(4,'little')+original[20:]}
malformed=[]
color_fixture={}
with tempfile.TemporaryDirectory() as temp:
    for name,data in mutations.items():
        path=Path(temp)/(name+'.m3');path.write_bytes(data)
        result=subprocess.run([str(args.exe.resolve()),str(path)],capture_output=True,text=True)
        assert result.returncode==1,(name,result.returncode,result.stderr)
        report=json.loads(result.stdout)
        assert report['failed']==1 and not report['files'][0]['ok']
        malformed.append(dict(name=name,rejected=True,error=report['files'][0]['error']))
    doc=io_m3.M3SectionList.load(str(source),lazy=False);model=doc.model
    old_desc=io_m3.M3StructureDescription.get_vertex_description(model.vertex_flags)
    assert not model.vertex_flags&0x200
    new_desc=io_m3.M3StructureDescription.get_vertex_description(model.vertex_flags|0x200)
    color_offset=0
    for name,field in new_desc.fields.items():
        if name=='col':break
        color_offset+=field.size
    raw=bytes(doc[model.vertices].raw_bytes[:model.vertices.entries]);colored=bytearray()
    for offset in range(0,len(raw),old_desc.size):colored.extend(raw[offset:offset+color_offset]+bytes([30,20,10,40])+raw[offset+color_offset:offset+old_desc.size])
    doc[model.vertices].content=list(colored);model.vertices.entries=len(colored);model.vertex_flags|=0x200
    fixture=Path(temp)/'vertex-color.m3';doc.save(str(fixture))
    result=subprocess.run([str(args.exe.resolve()),str(fixture),'--geometry'],capture_output=True,text=True,check=True)
    checked=json.loads(result.stdout)['files'][0]['vertexData'];assert checked
    for vertex in checked:assert vertex['hasColor'] and vertex['color']==[10,20,30,40]
    color_fixture=dict(vertices=len(checked),rgba=[10,20,30,40],passed=True)
args.output.write_text(json.dumps(dict(complete=True,tool=str(args.exe.resolve()),toolSha256=hashlib.sha256(args.exe.read_bytes()).hexdigest(),files=rows,total=len(rows),passed=len(rows),malformed=malformed,colorFixture=color_fixture,
    scope='Full vertex position/normal/weights/palette-index/UV values; skeleton names/parents/flags/channel IDs; sequence names/timing/flags; region counts. No renderer, material, playback or semantic parity claim.'),indent=2))
print(f'Native/reference reader parity: {len(rows)} real files; {len(malformed)} malformed inputs rejected')

