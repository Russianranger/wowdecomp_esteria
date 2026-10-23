"""Extract named MADD inputs from an already extracted local HotS shader cache.

Research only: the client uses the small checked-in metadata, not this cache.
"""
import argparse
import hashlib
import json
import struct
from pathlib import Path

p = argparse.ArgumentParser()
p.add_argument("--cache", type=Path, required=True)
p.add_argument("--materials", type=Path, required=True)
p.add_argument("--output", type=Path, required=True)
a = p.parse_args()
b = a.cache.read_bytes()
assert 40 <= len(b) <= 64*1024*1024
materials = json.loads(a.materials.read_text(encoding="utf-8-sig"))
wanted = {g["id"]: {f["id"] for f in g["parameters"]}
          for m in materials["records"] for g in m["groups"]}
qualities = []
for quality in range(4):
    start, length = struct.unpack_from("<II", b, 8+8*quality)
    assert 40 <= start < start+length <= len(b)
    cursor = start+8
    tables = {}
    for magic, stride in ((b"BSDD", 32), (b"CVDD", 32), (b"CPDD", 32),
                          (b"BPDD", 1), (b"VDDD", 1), (b"TSDD", 1)):
        tag, count = struct.unpack_from("<4sI", b, cursor)
        assert tag == magic, (quality, cursor, tag, magic)
        data = cursor+8
        cursor = data+count*stride
        assert cursor <= start+length
        tables[magic] = (data, count)
    strings, string_bytes = tables[b"TSDD"]
    def text(offset):
        assert offset < string_bytes
        end = b.index(0, strings+offset, strings+string_bytes)
        return b[strings+offset:end].decode("ascii")
    blocks, block_count = tables[b"BSDD"]
    parameters, parameter_bytes = tables[b"BPDD"]
    defaults, default_bytes = tables[b"VDDD"]
    groups = []
    for i in range(block_count):
        at = blocks+32*i
        name, ident, count, flags, pointer, unused = struct.unpack_from("<QIHHQQ", b, at)
        if ident not in wanted:
            continue
        assert pointer+40*count <= parameter_bytes
        fields = []
        for j in range(count):
            where = parameters+pointer+40*j
            nm, reserved1, reserved2, fid, value_offset, kind, reserved3 = struct.unpack_from("<QQQIIII", b, where)
            assert value_offset <= default_bytes
            fields.append(dict(id=fid, name=text(nm), kind=kind,
                               valueOffset=value_offset, sourceOffset=where))
        assert wanted[ident] <= {f["id"] for f in fields}, (quality, text(name), wanted[ident])
        groups.append(dict(id=ident, name=text(name), flags=flags, fields=fields, sourceOffset=at))
    assert {g["id"] for g in groups} == wanted.keys()
    qualities.append(dict(quality=quality, groups=groups))
# Names/types used by the model must agree across the four quality sections.
canonical = qualities[0]["groups"]
for q in qualities[1:]:
    other = {g["id"]:g for g in q["groups"]}
    for group in canonical:
        compare = other[group["id"]]
        assert group["name"] == compare["name"]
        fs = {f["id"]:f for f in compare["fields"]}
        for f in group["fields"]:
            if f["id"] in wanted[group["id"]]:
                assert (f["name"], f["kind"]) == (fs[f["id"]]["name"], fs[f["id"]]["kind"])
result = dict(cacheSHA256=hashlib.sha256(b).hexdigest(), qualities=qualities,
              scope="Named shader block and parameter metadata; runtime values still require interpretation and rendering verification.")
a.output.write_text(json.dumps(result, indent=2), encoding="utf-8")
print(json.dumps(dict(qualities=len(qualities), groups=len(canonical), fields=sum(len(g['fields']) for g in canonical))))
