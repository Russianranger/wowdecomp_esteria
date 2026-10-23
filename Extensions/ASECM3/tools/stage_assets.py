"""Stage owned native M3/M3A/DDS assets byte-for-byte with provenance hashes."""
import argparse,hashlib,json,shutil
from pathlib import Path,PurePosixPath,PureWindowsPath
p=argparse.ArgumentParser();p.add_argument('--models',type=Path,required=True);p.add_argument('--model-root',type=Path,required=True);p.add_argument('--textures',type=Path,required=True);p.add_argument('--references',type=Path,required=True);p.add_argument('--output',type=Path,required=True);a=p.parse_args()
models=json.loads(a.models.read_text());textures=json.loads(a.textures.read_text());references=json.loads(a.references.read_text())
assert models.get('complete') and textures.get('complete'),'Require completed reader and DDS reports'
root=a.output.resolve();source_root=a.model_root.resolve();root.mkdir(parents=True,exist_ok=True)
files=[];destinations={}
def digest(path):
 h=hashlib.sha256()
 with path.open('rb') as f:
  for data in iter(lambda:f.read(1024*1024),b''):h.update(data)
 return h.hexdigest()
def stage(source,relative,expected,kind):
 source=source.resolve();target=(root/relative).resolve();target.relative_to(root)
 key=str(relative).casefold()
 if key in destinations:
  assert destinations[key]==expected,'Conflicting texture references for '+str(relative)
  return
 assert digest(source)==expected,'Source changed after verification: '+str(source)
 target.parent.mkdir(parents=True,exist_ok=True)
 if target.exists():assert digest(target)==expected,'Existing stage file differs: '+str(target)
 else:shutil.copy2(source,target)
 assert digest(target)==expected,'Staged readback mismatch: '+str(target)
 destinations[key]=expected
 files.append(dict(kind=kind,path=relative.as_posix(),source=str(source),bytes=target.stat().st_size,sha256=expected))
for model in models['files']:
 assert model['passed']
 source=Path(model['path']);relative=source.resolve().relative_to(source_root)
 stage(source,relative,model['sha256'],'native-model-or-bank')
lookup={str(Path(f['path']).resolve()).casefold():f for f in textures['files']}
for ref in references['files']:
 source=Path(ref['path']);proof=lookup[str(source.resolve()).casefold()];assert proof['passed']
 virtual=ref['reference'].replace('\\','/');name=PurePosixPath(virtual)
 assert not PureWindowsPath(virtual).drive and not name.is_absolute() and '..' not in name.parts,'Unsafe source reference'
 stage(source,Path(*name.parts),proof['sha256'],'native-dds-texture')
manifest=dict(complete=True,files=files,total=len(files),bytes=sum(f['bytes'] for f in files),
 scope='Exact source assets only; no M2 conversion or modified original models. Staging is not client installation or rendering proof.')
(root/'ASECM3-ASSETS.json').write_text(json.dumps(manifest,indent=2))
print('Staged',len(files),'source assets;',round(manifest['bytes']/1048576,2),'MiB; all hashes read back')
