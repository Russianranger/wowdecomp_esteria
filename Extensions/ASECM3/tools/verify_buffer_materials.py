"""Compare indexed MADD storage with bytes fetched by the source M3 parser.

This checks structural decoding only. IDs and byte widths are not shader
semantics, and none of these tests makes a rendered MADD fidelity claim.
"""
import argparse
import hashlib
import json
import struct
import subprocess
import sys
import tempfile
from pathlib import Path

p = argparse.ArgumentParser()
for name in ("exe", "reference", "corpus", "output"):
    p.add_argument("--" + name, type=Path, required=True)
a = p.parse_args()
sys.path.insert(0, str(a.reference.resolve()))
import io_m3

rows = []
malformed_seed = None
files = fields = groups = 0
for path in sorted(a.corpus.rglob("*")):
    if path.suffix.lower() not in (".m3", ".m3a"):
        continue
    run = subprocess.run([str(a.exe.resolve()), str(path)], capture_output=True, text=True)
    assert run.returncode == 0, (path, run.stdout, run.stderr)
    native = json.loads(run.stdout)
    source = io_m3.M3SectionList.load(str(path), lazy=False)
    model = source[source[0][0].model][0]
    files += 1
    for material in native["materials"]:
        if material["type"] != 12:
            assert material["bufferGroups"] == [] and material["bufferDecodeError"] == ""
            continue
        record = source[model.materials_buffer][material["sourceIndex"]]
        data = bytes(source[record.unknown4].raw_bytes[:record.unknown4.entries])
        expected_ids = list(source[record.unknown2])
        if malformed_seed is None:
            malformed_seed = (path, source[record.unknown4].index_entry.offset, data)
        assert material["bufferDecodeError"] == "", (path, material["bufferDecodeError"])
        count, id_offset, group_offset = struct.unpack_from("<IQQ", data)
        assert count == len(expected_ids) == len(material["bufferGroups"])
        for i, group in enumerate(material["bufferGroups"]):
            ident = struct.unpack_from("<I", data, id_offset + 4*i)[0]
            start = struct.unpack_from("<Q", data, group_offset + 8*i)[0]
            assert group["id"] == expected_ids[i] == ident and group["sourceOffset"] == start
            n, keys, widths, flags, values = struct.unpack_from("<IQQQQ", data, start)
            assert n == len(group["parameters"])
            for j, field in enumerate(group["parameters"]):
                key = struct.unpack_from("<I", data, keys + 4*j)[0]
                width = struct.unpack_from("<H", data, widths + 2*j)[0]
                offset = struct.unpack_from("<Q", data, values + 8*j)[0]
                assert field == dict(id=key, byteWidth=width, flags=data[flags+j],
                                     sourceOffset=offset, bytes=data[offset:offset+width].hex())
                fields += 1
            groups += 1
        rows.append(dict(path=str(path), sha256=hashlib.sha256(path.read_bytes()).hexdigest(),
                         material=material["name"], version=material["record"]["version"],
                         texturePaths=material["bufferTextures"], groups=material["bufferGroups"]))
malformed = []
assert malformed_seed is not None, "No source MADD buffer was checked"
path, payload_start, original = malformed_seed
_, ids, offsets = struct.unpack_from("<IQQ", original)
mutations = [("count_budget", 0, "<I", 4097),
             ("offset_overflow", 4, "<Q", 2**64-1),
             ("identifier_mismatch", ids, "<I", 0),
             ("group_alias", offsets+8, "<Q", struct.unpack_from("<Q", original, offsets)[0])]
with tempfile.TemporaryDirectory() as temp:
    for name, at, fmt, value in mutations:
        full = bytearray(path.read_bytes())
        struct.pack_into(fmt, full, payload_start+at, value)
        candidate = Path(temp)/(name+".m3")
        candidate.write_bytes(full)
        run = subprocess.run([str(a.exe.resolve()), str(candidate)], capture_output=True, text=True)
        assert run.returncode == 0, (name, run.stdout, run.stderr)
        record = next(m for m in json.loads(run.stdout)["materials"] if m["type"] == 12)
        assert record["bufferDecodeError"] and record["bufferGroups"] == []
        retained = next(b for b in record["bufferPayloads"] if b["role"] == "unknown4")
        assert bytes.fromhex(retained["bytes"]) == full[payload_start:payload_start+len(original)]
        malformed.append(dict(case=name, diagnostic=record["bufferDecodeError"], originalPayloadRetained=True))
report = dict(complete=True, files=files, materials=len(rows), groups=groups, fields=fields,
              toolSHA256=hashlib.sha256(a.exe.read_bytes()).hexdigest(), records=rows,
              malformedBuffers=malformed,
              scope="Exact indexed MADD group/field IDs, byte widths, flags, offsets and value bytes; source M3 parser supplies payload bytes. Shader roles and rendering are unverified.")
a.output.write_text(json.dumps(report, indent=2), encoding="utf-8")
print(json.dumps({k: report[k] for k in ("complete", "files", "materials", "groups", "fields")}))
