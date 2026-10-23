r"""Pack fetched SC2 files into a test MPQ for the M3 module.

Layout inside the archive matches what the client asks for: the model at SC2\...\<name>.m3 and textures at
ASECM3\Assets\Textures\ (the module's texture fallback). Usage:
  pack_sc2_test.py --assets <dir with Assets/...> --archive <Data>/Patch-ZzzSC2.MPQ --tools <EsteriaWoW tools>
"""
import argparse, sys
from pathlib import Path

p = argparse.ArgumentParser()
p.add_argument("--assets", type=Path, required=True)
p.add_argument("--archive", type=Path, required=True)
p.add_argument("--dbc", type=Path, help="folder of .dbc files to add under DBFilesClient")
p.add_argument("--tools", type=Path, default=Path(r"R:\Users\Zach\Documents\GitHub\EsteriaWoW\tools"))
a = p.parse_args()
sys.path.insert(0, str(a.tools))
from cars_mount_pack import DLL_DEFAULT, Storm  # noqa: E402

entries = {}
for f in sorted(a.assets.rglob("*")):
    if not f.is_file():
        continue
    rel = f.relative_to(a.assets)
    if rel.suffix.lower() in (".m3", ".m3a"):
        name = "SC2\\" + "\\".join(rel.parts[1:] if rel.parts[0].lower() == "assets" else rel.parts)
    elif rel.suffix.lower() == ".dds":
        name = "ASECM3\\" + "\\".join(rel.parts)
    else:
        continue
    entries[name] = f.read_bytes()
if a.dbc:
    for f in sorted(a.dbc.glob("*.dbc")):
        entries["DBFilesClient\\" + f.name] = f.read_bytes()
assert any(n.endswith(".m3") for n in entries), "no model found"
if a.archive.exists():
    raise SystemExit(f"{a.archive} exists; remove it first")
storm = Storm(DLL_DEFAULT)
storm.create_archive(a.archive, entries)
h = storm.open_archive(a.archive)
for n, payload in entries.items():
    assert storm.read(h, n) == payload, n
    print("ok", n, len(payload))
storm.dll.SFileCloseArchive(h)
