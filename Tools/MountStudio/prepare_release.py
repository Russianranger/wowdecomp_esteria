"""Stage Mount Studio's relocatable runtime and record every shipped dependency."""
from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path, PurePosixPath
import shutil
import subprocess
import sys
import urllib.request
import zipfile

ROOT = Path(__file__).resolve().parent
PYTHON_VERSION = "3.13.7"
PYTHON_SHA256 = "f6cca216a359be84797cabb54149ce5e062afb16cc7567eb7fc51cacb2d86b65"
TABLES = ("Mount", "MountXDisplay", "CreatureDisplayInfo", "CreatureModelData")
READER_FILES = ("errors.py", "log.py", "db/bits.py", "db/db2.py", "db/dbd.py")


def sha(path):
    with path.open("rb") as stream:
        return hashlib.file_digest(stream, "sha256").hexdigest()


def download(url, path, expected=None):
    if path.is_file() and (expected is None or sha(path) == expected):
        return
    path.parent.mkdir(parents=True, exist_ok=True)
    print(f"Fetching {url}", flush=True)
    request = urllib.request.Request(url, headers={"User-Agent": "EsteriaMountStudioBuild"})
    with urllib.request.urlopen(request, timeout=120) as response, path.open("wb") as stream:
        shutil.copyfileobj(response, stream)
    if expected and sha(path) != expected:
        raise ValueError(f"Downloaded checksum differs: {path.name}")


def stage(args):
    runtime = ROOT / "runtime"
    runtime.mkdir(exist_ok=True)
    cache = ROOT / "src-tauri/target/downloads"
    archive = cache / f"python-{PYTHON_VERSION}-embed-amd64.zip"
    download(f"https://www.python.org/ftp/python/{PYTHON_VERSION}/{archive.name}", archive, PYTHON_SHA256)
    python = runtime / "python"
    python.mkdir(exist_ok=True)
    with zipfile.ZipFile(archive) as source:
        for entry in source.infolist():
            name = PurePosixPath(entry.filename.replace("\\", "/"))
            if name.is_absolute() or ".." in name.parts or ":" in entry.filename:
                raise ValueError("Unsafe Python runtime archive path")
        source.extractall(python)
    # Isolated embedded Python must be able to import the shipped worker helpers; no host site-packages are used.
    (python / "python313._pth").write_text("python313.zip\n.\n..\n../..\n", encoding="ascii")
    backend = runtime / "backend"
    backend.mkdir(exist_ok=True)
    shutil.copy2(args.probe, backend / "RetailAssetProbe.exe")
    shutil.copy2(args.storm, backend / "StormLib.dll")
    redist_root = Path(r"C:\Program Files (x86)\Microsoft Visual Studio")
    redists = sorted(redist_root.glob("*/BuildTools/VC/Redist/MSVC/*/x64/Microsoft.VC*.CRT"))
    if not redists:
        raise ValueError("Visual C++ redistributable directory was not found")
    redist = max(redists, key=lambda path: tuple(int(value) for value in path.parents[1].name.split('.')))
    for dll in redist.glob("*.dll"):
        shutil.copy2(dll, backend / dll.name)
    reader = runtime / "reader/wotlkconv"
    (reader / "db").mkdir(parents=True, exist_ok=True)
    for name in READER_FILES:
        shutil.copy2(args.reader / "wotlkconv" / name, reader / name)
    for initializer in (reader / "__init__.py", reader / "db/__init__.py"):
        initializer.write_text('"""DB2-only reader snapshot; no asset conversion code is shipped."""\n',
                               encoding="utf-8")
    definitions = runtime / "definitions"
    definitions.mkdir(exist_ok=True)
    if args.definitions and args.definitions.is_dir():
        for name in TABLES:
            shutil.copy2(args.definitions / f"{name}.dbd", definitions / f"{name}.dbd")
        revision = args.definitions / "source-revision.json"
        if revision.exists():
            shutil.copy2(revision, definitions / revision.name)
    else:
        request = urllib.request.Request("https://api.github.com/repos/wowdev/WoWDBDefs/commits/master",
                                         headers={"User-Agent": "EsteriaMountStudioBuild"})
        with urllib.request.urlopen(request, timeout=60) as response:
            revision = json.load(response)["sha"]
        for name in TABLES:
            download(f"https://raw.githubusercontent.com/wowdev/WoWDBDefs/{revision}/definitions/{name}.dbd",
                     definitions / f"{name}.dbd")
        (definitions / "source-revision.json").write_text(json.dumps({"revision": revision}) + "\n")
    config = runtime / "config"
    config.mkdir(exist_ok=True)
    shutil.copy2(ROOT / "config/RetailCasc.ini", config / "RetailCasc.ini")
    # Mount IDs are server metadata, not display IDs. Ship a read-only snapshot so Docker is optional.
    sys.path.insert(0, str(ROOT.parent / "RetailAssets"))
    from server import query
    mappings = {}
    for line in query("SELECT CreatureID, CreatureDisplayID FROM creature_template_model "
                      "WHERE Probability > 0 ORDER BY CreatureID, Idx;").splitlines():
        creature, display = map(int, line.split("\t"))
        if display:
            mappings.setdefault(str(creature), []).append(display)
    (runtime / "mount-models.json").write_text(json.dumps(mappings, sort_keys=True) + "\n", encoding="utf-8")
    licenses = runtime / "licenses"
    licenses.mkdir(exist_ok=True)
    download("https://raw.githubusercontent.com/ladislav-zezula/StormLib/master/LICENSE", licenses / "StormLib.txt")
    casc_license = next((ROOT.parents[1] / "build/retail-research").glob("CascLib-*/LICENSE"), None)
    if casc_license:
        shutil.copy2(casc_license, licenses / "CascLib.txt")
    notices = ("Python: Python Software Foundation license, see python/LICENSE.txt.\n"
               "StormLib and CascLib: upstream license texts in this folder.\n"
               "DB2 reader: original wotlkconv source headers are retained in reader/.\n"
               "WoWDBDefs: wowdev/WoWDBDefs; pinned schema revision in definitions/.\n"
               "RetailAssetProbe: built from the adjacent wowdecomp source checkout.\n"
               "Microsoft VC++ runtime: Visual Studio redistributable components.\n")
    (licenses / "THIRD-PARTY.txt").write_text(notices, encoding="utf-8")
    required = ["python/python.exe", "backend/StormLib.dll", "backend/RetailAssetProbe.exe",
                "reader/wotlkconv/db/db2.py", "config/RetailCasc.ini", "mount-models.json"]
    for relative in required:
        if not (runtime / relative).is_file():
            raise ValueError(f"Missing runtime dependency: {relative}")
    result = subprocess.run([str(python / "python.exe"), "-B", "-c",
                             "import sys; sys.path.insert(0,sys.argv[1]); "
                             "from wotlkconv.db.db2 import parse_db2; import ctypes,ssl,hashlib; "
                             "ctypes.WinDLL(sys.argv[2]); print('Embedded runtime and reader passed')",
                             str(runtime / "reader"), str(backend / "StormLib.dll")], check=True,
                            capture_output=True, text=True)
    print(result.stdout.strip())
    for compiled in runtime.rglob("*.pyc"):
        compiled.unlink()
    for directory in sorted(runtime.rglob("__pycache__"), reverse=True):
        directory.rmdir()
    capabilities = subprocess.run([str(backend / "RetailAssetProbe.exe"), "--capabilities"], check=True,
                                  capture_output=True, text=True)
    if not json.loads(capabilities.stdout).get("table_extract"):
        raise ValueError("Bundled native probe does not support metadata extraction")
    manifest = {"python": PYTHON_VERSION, "python_archive_sha256": PYTHON_SHA256,
                "files": {path.relative_to(runtime).as_posix(): {"sha256": sha(path), "size": path.stat().st_size}
                          for path in sorted(runtime.rglob("*")) if path.is_file() and path.name != "manifest.json"
                          and "__pycache__" not in path.parts}}
    (runtime / "manifest.json").write_text(json.dumps(manifest, indent=2) + "\n", encoding="utf-8")
    total = sum(value['size'] for value in manifest['files'].values())
    print(f"Staged {len(manifest['files'])} runtime files ({total:,} bytes)")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--probe", type=Path,
                        default=ROOT.parents[1] / "build/retail-assets/Release/RetailAssetProbe.exe")
    parser.add_argument("--storm", type=Path, default=Path(
        r"R:\Users\Zach\Downloads\battlemon\Client\wdbx-2.4.1.a-extended-dbc\x64\StormLib.dll"))
    parser.add_argument("--reader", type=Path, default=ROOT.parents[2] / "EsteriaWoW/RaceGuide/sources/Converter")
    parser.add_argument("--definitions", type=Path, default=Path(r"G:\MountStudioWork\definitions"))
    stage(parser.parse_args())
