"""Embed the checked, named MADD shader inputs without a runtime cache dependency."""
import hashlib
import json
from pathlib import Path
root = Path(__file__).resolve().parents[1]
source = root/"data/madd_shader_inputs.json"
data = json.loads(source.read_text())
lines = ["// Input SHA256: madd_shader_inputs.json "+hashlib.sha256(source.read_bytes()).hexdigest(),
         "// Named parameter metadata extracted from four agreeing HotS shader-cache sections.",
         "constexpr BufferGroupName bufferGroupNames[]={"]
for group in data["groups"]:
    lines.append("{%du,%s}," % (group["id"], json.dumps(group["name"])))
lines.append("};\nconstexpr BufferInputName bufferInputNames[]={")
for group in data["groups"]:
    for field in group["fields"]:
        lines.append("{%du,%du,%du,%s}," % (group["id"], field["id"], field["kind"], json.dumps(field["name"])))
lines.append("};\n")
(root/"src/BufferInputs.generated.inc").write_text("\n".join(lines), encoding="utf-8")
