r"""Add the M3 module's translated shader records to a client's Vulkan shader folder(s).

The module ships its own D3D9 shader bytecode (client/*.generated.inc); Tools/VulkanShaders translates it to SPIR-V
with --extra-vertex/--extra-pixel. This copies the five translated modules into <client>/VulkanShaders and
<client>/VulkanShadersRetail and appends their manifest blocks, replacing any earlier copy. Existing records and
modules are untouched, so executables that do not know the records keep working.

  python install_m3_vulkan_shaders.py --translated <tree from translate.py --out> --client <client folder>
"""
import argparse, re, shutil
from pathlib import Path

p = argparse.ArgumentParser()
p.add_argument("--translated", type=Path, required=True)
p.add_argument("--client", type=Path, required=True)
a = p.parse_args()

text = (a.translated / "manifest.txt").read_text(encoding="utf-8")
def block_lines(b):
    return "\n".join(l for l in b.splitlines() if not l.lstrip().startswith("#")).strip("\n")


blocks = [x for x in (block_lines(b) for b in re.split(r"\n\s*\n", text)) if x.startswith("record ")]
assert len(blocks) == 5, len(blocks)
md5s = [b.split()[1] for b in blocks]

for name in ("VulkanShaders", "VulkanShadersRetail"):
    folder = a.client / name
    manifest = folder / "manifest.txt"
    if not manifest.is_file():
        continue
    original = manifest.read_text(encoding="utf-8")
    kept = [b for b in re.split(r"\n\s*\n", original) if not any(("record %s " % m) in b for m in md5s) and not re.search(r"^name (surface|distortion)\w*(:\d+)?$", b, re.M)]
    backup = folder / "manifest.txt.before-m3"
    if not backup.exists():
        shutil.copy2(manifest, backup)
    manifest.write_text("\n\n".join(x.strip("\n") for x in kept if x.strip()) + "\n\n" + "\n\n".join(blocks) + "\n", encoding="utf-8", newline="\n")
    for kind in ("vertex", "pixel"):
        for spv in (a.translated / kind).glob("*.spv"):
            shutil.copy2(spv, folder / kind / spv.name)
    print("updated", folder, "with", len(blocks), "records")
