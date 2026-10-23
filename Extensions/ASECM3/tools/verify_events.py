"""Compare native event fields with the independent local io_m3 source reader."""
import argparse
import hashlib
import json
import subprocess
import sys
from pathlib import Path

p = argparse.ArgumentParser()
for name in ('exe', 'reference', 'corpus', 'output'):
    p.add_argument('--'+name, type=Path, required=True)
a = p.parse_args()
sys.path.insert(0, str(a.reference.resolve()))
import io_m3
run = subprocess.run([str(a.exe.resolve()), str(a.corpus.resolve())], capture_output=True, text=True)
assert run.returncode == 0, (run.stdout, run.stderr)
native = json.loads(run.stdout)
assert native['clockChecks'] and not native['failed']
files = []
events = integers = 0
normalize = lambda rows: sorted(json.dumps(row, sort_keys=True) for row in rows)
for f in native['files']:
    path = Path(f['path'])
    d = io_m3.M3SectionList.load(str(path), lazy=False)
    text = lambda ref: d[ref].content_to_string() if ref.entries else ''
    model = d[d[0][0].model][0]
    expected = []
    for c in d[model.sequence_transformation_collections]:
        row = dict(name=text(c.name), events=[], integers=[])
        for ident, ref in zip(d[c.anim_ids], d[c.anim_refs]):
            kind, index = ref >> 16, ref & 65535
            if kind == 0:
                block = d[c.sdev][index]
                for time, event in zip(d[block.frames], d[block.keys]):
                    row['events'].append(dict(track=ident, time=time, name=text(event.name),
                        id=event.id, bone=event.bone, flags=event.flags, payload=text(event.payload),
                        param0=getattr(event, 'data_param0', 0), param1=getattr(event, 'data_param1', 0)))
            elif kind == 10:
                block = d[c.sdu3][index]
                ordered = dict(zip(d[block.frames], d[block.keys]))
                row['integers'].append(dict(id=ident, times=sorted(ordered), values=[ordered[t] for t in sorted(ordered)]))
        expected.append(row)
    assert len(expected) == len(f['collections'])
    for source, actual in zip(expected, f['collections']):
        assert source['name'] == actual['name']
        assert normalize(source['events']) == normalize(actual['events']), (path, source['name'])
        assert normalize(source['integers']) == normalize(actual['integers']), (path, source['name'])
        events += len(source['events'])
        integers += len(source['integers'])
    files.append(dict(path=str(path), sha256=hashlib.sha256(path.read_bytes()).hexdigest(), passed=True))
result = dict(files=len(files), events=events, unsignedTracks=integers, sourceParity=True, clockPartition=True,
              records=files, toolSHA256=hashlib.sha256(a.exe.read_bytes()).hexdigest(),
              scope='Independent source event IDs, bones, flags, names/payloads, optional parameters, track IDs/times and unsigned keys; native clock checks. Not matrix/raw-byte or actor behavior parity.')
a.output.write_text(json.dumps(result, indent=2))
print(json.dumps(dict(files=len(files), events=events, unsignedTracks=integers, passed=True)))
