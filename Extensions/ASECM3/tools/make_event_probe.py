"""Create a labelled, derived event probe; never modify the supplied model.

The stock corpus has SeqEnd but no Simulate events. This fixture changes the
event name and gives its SDEV record a private 100-ms time section, preserving
all original bone/material track times. It is diagnostic data, not a game asset.
"""
import argparse
import hashlib
import json
import struct
import sys
from pathlib import Path

p = argparse.ArgumentParser()
p.add_argument('--model', type=Path, required=True)
p.add_argument('--reference', type=Path, required=True)
p.add_argument('--output', type=Path, required=True)
a = p.parse_args()
assert a.model.resolve() != a.output.resolve()
sys.path.insert(0, str(a.reference.resolve()))
import io_m3
d = io_m3.M3SectionList.load(str(a.model), lazy=False)
m = d[d[0][0].model][0]
data = bytearray(a.model.read_bytes())
index, count = struct.unpack_from('<II', data, 4)
assert index + count * 16 == len(data)
selected = None
for c in d[m.sequence_transformation_collections]:
    for ref in d[c.anim_refs]:
        if ref >> 16 != 0:
            continue
        block = d[c.sdev][ref & 65535]
        if block.keys.entries == 1:
            key = d[block.keys][0]
            if d[key.name].content_to_string() == 'Evt_SeqEnd':
                selected = (c, ref & 65535, block, key)
                break
    if selected:
        break
assert selected, 'No single-key source event available'
c, ordinal, block, key = selected
char_section = key.name.index
name = b'Evt_Simulate\0'
where = d[char_section].index_entry.offset
next_offsets = [s.index_entry.offset for s in d if s.index_entry.offset > where] + [index]
assert len(name) <= min(next_offsets) - where
data[where:where+len(name)] = name
struct.pack_into('<I', data, index+char_section*16+8, len(name))
changed = 0
for s in d:
    if s.desc.history.name != 'EVNT':
        continue
    for i, event in enumerate(s):
        if event.name.index == char_section:
            struct.pack_into('<I', data, s.index_entry.offset+i*s.desc.size, len(name))
            changed += 1
assert changed
# A new I32 section avoids changing a shared source endpoint time array.
sdev = d[c.sdev]
struct.pack_into('<I', data, sdev.index_entry.offset+ordinal*sdev.desc.size+4, count)
entries = bytes(data[index:index+count*16])
data = data[:index] + struct.pack('<i', 100) + entries
data += struct.pack('<4sIII', b'_23I', index, 1, 0)
struct.pack_into('<II', data, 4, index+4, count+1)
a.output.parent.mkdir(parents=True, exist_ok=True)
a.output.write_bytes(data)
report = dict(source=str(a.model), sourceSHA256=hashlib.sha256(a.model.read_bytes()).hexdigest(),
              fixture=str(a.output), fixtureSHA256=hashlib.sha256(data).hexdigest(),
              changedEventReferences=changed, event='Evt_Simulate', time=100,
              scope='Derived diagnostic fixture; source model and bone/material timelines unchanged')
a.output.with_suffix('.json').write_text(json.dumps(report, indent=2))
print(json.dumps(report))
