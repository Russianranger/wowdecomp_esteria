"""Check named MADD input routing against source bytes and cache metadata.

The oracle checks the documented preview adapter policy. It is not an oracle
for HotS lighting, ECSpec, HDR, or rendered source-model pixel fidelity.
"""
import argparse
import hashlib
import json
import math
import struct
import subprocess
import sys
import tempfile
from pathlib import Path

p=argparse.ArgumentParser()
for name in ('exe','reference','corpus','metadata','output'):
    p.add_argument('--'+name,type=Path,required=True)
a=p.parse_args()
sys.path.insert(0,str(a.reference.resolve()))
import io_m3
metadata=json.loads(a.metadata.read_text())
named={g['id']:g for g in metadata['groups']}

def inspect(path):
    run=subprocess.run([str(a.exe.resolve()),str(path)],capture_output=True,text=True)
    assert run.returncode==0,(path,run.stdout,run.stderr)
    return json.loads(run.stdout)

def props(record):return {p['name']:p['value'] for p in record['properties']}
def compare(actual,expected):
    assert actual.keys()==expected.keys(),(actual,expected)
    for key,want in expected.items():
        assert math.isclose(actual[key],want,rel_tol=1e-6,abs_tol=1e-6),(key,actual[key],want)

mapping={'Normal':('Normal','TexNormal','layer_norm'),
         'Gloss':('Gloss','TexGloss','layer_gloss'),
         'Specular':('Specular','TexSpecular','layer_spec'),
         'DiffuseTeamColor':('Diffuse','TexDiffuse','layer_diff'),
         'EmissiveFast':('Emissive','TexEmissive','layer_emis1'),
         'EnvioMask':('EnvioMask','TexEnvioMask','layer_envi_mask'),
         'EnvioBlurred':('EnvironmentMap','TexEnvironmentMap','layer_envi')}
rows=[];files=layers=0;seed=None
for path in sorted(a.corpus.rglob('*')):
    if path.suffix.lower() not in ('.m3','.m3a'):continue
    native=inspect(path);files+=1
    d=io_m3.M3SectionList.load(str(path),lazy=False);model=d[d[0][0].model][0]
    for mat in native['materials']:
        if mat['type']!=12:
            assert mat['bufferSurface'] is None and mat['bufferSurfaceError']==''
            continue
        source=d[model.materials_buffer][mat['sourceIndex']]
        raw=bytes(d[source.unknown4].raw_bytes[:source.unknown4.entries])
        count,keys,pointers=struct.unpack_from('<IQQ',raw)
        expected_layers=[];expected_props={'hdr_spec':1,'hdr_emis':1,'flags':0}
        addresses={}
        for i in range(count):
            group_id=struct.unpack_from('<I',raw,keys+4*i)[0]
            group=named[group_id];start=struct.unpack_from('<Q',raw,pointers+8*i)[0]
            n,ids,widths,flags,values=struct.unpack_from('<IQQQQ',raw,start)
            inputs={};names={f['id']:f['name'] for f in group['fields']}
            for j in range(n):
                ident=struct.unpack_from('<I',raw,ids+4*j)[0]
                size=struct.unpack_from('<H',raw,widths+2*j)[0]
                offset=struct.unpack_from('<Q',raw,values+8*j)[0]
                name=names[ident];inputs[name]=raw[offset:offset+size]
                addresses[name]=(offset,ids+4*j)
            stage=group['name']
            if stage=='TwoSided':expected_props['flags']=8
            if stage=='LightingDeferred':expected_props['specularity']=struct.unpack('<f',inputs['Specularity'])[0]
            if stage not in mapping:continue
            prefix,texture,role=mapping[stage]
            index,sampler=struct.unpack('<II',inputs[texture]);assert sampler==3
            image=d[source.texture_paths][index].path
            path_value=d[image].content_to_string()
            lp={'flags':12,'uv_source':2 if stage=='EnvioBlurred' else [0,1,9,10][struct.unpack('<I',inputs[prefix+'UVSelection'])[0]]}
            transform=inputs['EnvironmentMapUVWTransform' if stage=='EnvioBlurred' else prefix+'UVTransform']
            assert struct.unpack('<8f',transform)==(0,0,1,1,0,0,0,0)
            if stage!='Normal':
                selector,multiply,add,brightness,invert,clamp,padding=struct.unpack('<IfffBBH',inputs[prefix+'ChannelSelection'])
                assert invert==0 and clamp==1
                lp.update(color_channels=[1,0,3,4,5,2][selector],asec_color_clamp=1,**{'color_multiply.default':multiply,'color_add.default':add,'color_brightness.default':brightness})
            if stage=='EmissiveFast':expected_props['blend_mode_emis1']=struct.unpack('<I',inputs['EmissiveControl'])[0]
            if stage=='EnvioBlurred':expected_props['blend_mode_layer']=struct.unpack('<I',inputs['EnvioControl'])[0]
            expected_layers.append((role,path_value,lp))
        assert mat['bufferSurfaceError']=='',(path,mat['bufferSurfaceError'])
        surface=mat['bufferSurface'];assert surface
        compare(props(surface['record']),expected_props)
        assert len(surface['layers'])==len(expected_layers)
        for got,(role,image,expected) in zip(surface['layers'],expected_layers):
            assert got['role']==role and got['image']==image
            compare(props(got['record']),expected)
        layers+=len(expected_layers)
        rows.append(dict(path=str(path),sha256=hashlib.sha256(path.read_bytes()).hexdigest(),material=mat['name'],layers=len(expected_layers),passed=True))
        if seed is None:seed=(path,d[source.unknown4].index_entry.offset,addresses)

assert seed
path,base,addresses=seed
mutations=[('texture_index','TexDiffuse',0,'<I',0xffffffff),
           ('sampler_flags','TexDiffuse',4,'<I',0),
           ('unknown_field','TexDiffuse','id','<I',0),
           ('channel_range','DiffuseChannelSelection',0,'<I',6),
           ('invert','DiffuseChannelSelection',16,'<B',1),
           ('clamp','DiffuseChannelSelection',17,'<B',0),
           ('uv_transform','DiffuseUVTransform',0,'<f',.5),
           ('uv_source','DiffuseUVSelection',0,'<I',9),
           ('normal_uv','NormalUVSelection',0,'<I',1),
           ('specular_nan','Specularity',0,'<f',float('nan')),
           ('emissive_mode','EmissiveControl',0,'<I',9),
           ('environment_mode','EnvioControl',0,'<I',9)]
rejected=[];channel_cases=[]
with tempfile.TemporaryDirectory() as temp:
    for name,field,delta,fmt,value in mutations:
        original=bytearray(path.read_bytes());offset,ident=addresses[field]
        struct.pack_into(fmt,original,base+(ident if delta=='id' else offset+delta),value)
        file=Path(temp)/(name+'.m3');file.write_bytes(original)
        mat=next(m for m in inspect(file)['materials'] if m['type']==12)
        assert mat['bufferDecodeError']=='' and mat['bufferSurface'] is None and mat['bufferSurfaceError'],(name,mat)
        rejected.append(dict(case=name,diagnostic=mat['bufferSurfaceError']))
    # Explicitly exercise all six channel routes and non-default finite gains.
    for channel in range(6):
        original=bytearray(path.read_bytes());offset,_=addresses['DiffuseChannelSelection']
        struct.pack_into('<Ifff',original,base+offset,channel,2,.1,.5)
        file=Path(temp)/('channel'+str(channel)+'.m3');file.write_bytes(original)
        mat=next(m for m in inspect(file)['materials'] if m['type']==12)
        assert mat['bufferSurfaceError']==''
        diffuse=next(x for x in mat['bufferSurface']['layers'] if x['role']=='layer_diff')
        lp=props(diffuse['record']);assert lp['color_channels']==[1,0,3,4,5,2][channel]
        assert math.isclose(lp['color_add.default'],.1,abs_tol=1e-6) and lp['color_multiply.default']==2 and lp['color_brightness.default']==.5
        channel_cases.append(channel)
result=dict(complete=True,files=files,materials=len(rows),layers=layers,records=rows,unsupportedRejected=rejected,
            channelCases=channel_cases,toolSHA256=hashlib.sha256(a.exe.read_bytes()).hexdigest(),
            metadataSHA256=hashlib.sha256(a.metadata.read_bytes()).hexdigest(),
            scope='Named shader-input routing from source MADD bytes and cache metadata into the documented preview surface. Not source lighting/ECSpec/HDR or pixel fidelity.')
a.output.write_text(json.dumps(result,indent=2),encoding='utf-8')
print(json.dumps(dict(files=files,materials=len(rows),layers=layers,unsupportedRejected=len(rejected),channelCases=len(channel_cases))))
