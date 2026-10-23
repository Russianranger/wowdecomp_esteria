"""Check native DDS dimensions and every face/mip range on real dependencies."""
import argparse,hashlib,json,struct,subprocess,tempfile
from pathlib import Path
p=argparse.ArgumentParser();p.add_argument('--exe',type=Path,required=True);p.add_argument('--sources',type=Path,required=True);p.add_argument('--output',type=Path,required=True);a=p.parse_args()
rows=[]
for source in json.loads(a.sources.read_text())['files']:
 path=Path(source['path']);raw=path.read_bytes();result=subprocess.run([str(a.exe.resolve()),str(path)],capture_output=True,text=True)
 assert result.returncode==0,(path.name,result.stderr)
 native=json.loads(result.stdout)
 width,height=struct.unpack_from('<II',raw,12)[::-1];levels=max(1,struct.unpack_from('<I',raw,28)[0]);code=raw[84:88]
 block={b'DXT1':8,b'DXT3':16,b'DXT5':16}[code]
 faces=6 if struct.unpack_from('<I',raw,112)[0]&0x200 else 1
 assert (native['width'],native['height'],native['levels'],native['faces'])==(width,height,levels,faces)
 assert native['format']=={b'DXT1':'BC1',b'DXT3':'BC2',b'DXT5':'BC3'}[code]
 expected=[];offset=128
 for face in range(faces):
  w,h=width,height
  for level in range(levels):
   row=((w+3)//4)*block;size=row*((h+3)//4)
   expected.append(dict(face=face,level=level,width=w,height=h,offset=offset,size=size,rowBytes=row))
   assert offset+size<=len(raw);offset+=size;w=max(1,w//2);h=max(1,h//2)
 assert native['mips']==expected
 rows.append(dict(path=str(path),sha256=hashlib.sha256(raw).hexdigest(),format=native['format'],levels=levels,faces=faces,mipRanges=len(expected),trailingBytes=len(raw)-offset,passed=True))
source=Path(json.loads(a.sources.read_text())['files'][0]['path']).read_bytes()
mutations={'empty':b'','truncated_header':source[:127],'truncated_mip':source[:-1],
 'bad_signature':b'NOPE'+source[4:],'bad_mip_count':source[:28]+struct.pack('<I',1000)+source[32:],
 'zero_width':source[:16]+struct.pack('<I',0)+source[20:]}
rejected=[]
with tempfile.TemporaryDirectory() as folder:
 for name,data in mutations.items():
  path=Path(folder)/(name+'.dds');path.write_bytes(data)
  result=subprocess.run([str(a.exe.resolve()),str(path)],capture_output=True,text=True)
  assert result.returncode==1,(name,result.returncode)
  rejected.append(dict(name=name,error=result.stderr.strip(),rejected=True))
a.output.write_text(json.dumps(dict(complete=True,tool=str(a.exe.resolve()),toolSha256=hashlib.sha256(a.exe.read_bytes()).hexdigest(),files=rows,total=len(rows),passed=len(rows),malformed=rejected,
 scope='DDS metadata and owned payload ranges only. No decompression, GPU upload, shader color-space, material, or rendered visual validation.'),indent=2))
print('DDS PASS',len(rows),'real textures,',sum(r['mipRanges'] for r in rows),'face/mip ranges;',len(rejected),'malformed files rejected')
