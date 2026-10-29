"""Exercise a relocated runtime without host Python, Docker, PATH tools or checkout imports."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--project", type=Path, required=True)
    parser.add_argument("--destination", type=Path, required=True)
    args = parser.parse_args()
    source = Path(__file__).resolve().parents[1]
    destination = args.destination.resolve()
    destination.mkdir(parents=True, exist_ok=True)
    program = destination / "Mount Studio standalone"
    program.mkdir(exist_ok=True)
    shutil.copytree(source / "runtime", program / "runtime", dirs_exist_ok=True)
    shutil.copy2(source / "worker.py", program / "worker.py")
    shutil.copy2(source.parent / "RetailAssets/server.py", program / "server.py")
    snapshot = json.loads(args.project.read_text(encoding="utf-8"))
    original_work = args.project.parent
    work = destination / "Project"
    work.mkdir(exist_ok=True)
    originals = work / "originals"
    originals.mkdir(exist_ok=True)
    ready = next(row for row in snapshot["mounts"] if row["status"] == "validated")
    needed = {"921760", "1576117", "1108759", "1365368", *ready["files"]}
    for key in needed:
        path = original_work / "originals" / f"{key}.bin"
        assert hashlib.sha256(path.read_bytes()).hexdigest() == snapshot["assets"][key]["sha256"]
        shutil.copy2(path, originals / path.name)
    runtime = program / "runtime"
    settings = dict(snapshot["settings"], work=str(work), source_mode="cdn", retail_root="",
                    cdn_build=snapshot["source"]["build"], cdn_region="us", source_locale="2",
                    mapping_mode="bundled", mapping_file=str(runtime / "mount-models.json"),
                    probe=str(runtime / "backend/RetailAssetProbe.exe"), storm=str(runtime / "backend/StormLib.dll"),
                    reader=str(runtime / "reader"), definitions=str(runtime / "definitions"),
                    source_config=str(runtime / "config/RetailCasc.ini"), export_dir="", auto_open_export=False)
    snapshot["settings"] = settings
    (work / "project.json").write_text(json.dumps(snapshot), encoding="utf-8")
    env = {key: os.environ[key] for key in ("SYSTEMROOT", "WINDIR", "TEMP", "TMP", "USERPROFILE", "LOCALAPPDATA")
           if key in os.environ}
    env["PATH"] = str(Path(os.environ["SYSTEMROOT"]) / "System32")
    interpreter = runtime / "python/python.exe"
    def run(action, **extra):
        request = {"settings": settings, **extra}
        result = subprocess.run([str(interpreter), "-B", str(program / "worker.py"), action],
                                input=json.dumps(request), text=True, capture_output=True,
                                cwd=program, env=env, timeout=180)
        events = [json.loads(line) for line in result.stdout.splitlines()]
        if result.returncode:
            raise AssertionError((action, result.returncode, result.stdout[-5000:], result.stderr[-5000:]))
        print(action, "passed", flush=True)
        return events
    run("doctor")
    run("scan")
    run("catalog")
    current = json.loads((work / "project.json").read_text())
    edits = [{"key": row["key"], "selected": row["key"] == ready["key"],
              "mount": row["source"]["mount"] if row["source"] else None,
              "display": row["source"]["display"] if row["source"] else None} for row in current["mounts"]]
    run("download", edits=edits)
    events = run("export", ready_only=True)
    output = Path(next(event["path"] for event in events if event["type"] == "exported"))
    receipt = json.loads((output / "receipt.json").read_text())
    assert len(receipt["mounts"]) == 1 and len(receipt["archives"]) == 1
    assert (output / receipt["archives"][0]["file"]).is_file()
    print("Relocated embedded runtime completed scan -> metadata -> native validation -> compressed MPQ readback")
    print(output)


if __name__ == "__main__":
    main()
