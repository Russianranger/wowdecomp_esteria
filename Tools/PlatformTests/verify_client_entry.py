#!/usr/bin/env python3
"""Exercise only the real executable's explicit entry/fiber diagnostic path."""
import argparse
import hashlib
import importlib.util
import json
import os
from pathlib import Path
import platform
import shutil
import subprocess
import threading

FLAG = "--esteria-entry-check"
RECEIPT = "Esteria-entry-check.json"
CHECKS = ("imageEntryReached", "crtInitialized", "stormAllocator", "cppUnwind",
          "mainFiber", "fiberReturn", "fpControlRead")
LOG_LIMIT = 1024 * 1024


def require(condition, message):
    if not condition:
        raise ValueError(message)


def read_receipt(path, architecture):
    with path.open("rb") as stream:
        raw = stream.read(4097)
    require(len(raw) <= 4096, "entry receipt exceeds bound")
    data = json.loads(raw)
    require(isinstance(data, dict) and set(data) == {"schemaVersion", "scope", "architecture", "protocolBuild",
            "extensionVersion", "checks", "commonMainStarted", "assetsLoaded"}, "unexpected entry receipt fields")
    require(type(data.get("schemaVersion")) is int and data.get("schemaVersion") == 1 and data.get("scope") == "executable-entry-only" and
            data.get("architecture") == architecture and data.get("protocolBuild") == 12340 and
            data.get("extensionVersion") == 4, "wrong entry receipt identity")
    require(data.get("commonMainStarted") is False and data.get("assetsLoaded") is False,
            "entry test must not start game initialization")
    require(set(data.get("checks", {})) == set(CHECKS) and
            all(data["checks"][name] is True for name in CHECKS), "entry check failed or missing")
    return data, hashlib.sha256(raw).hexdigest()


def run_bounded(command, directory, log_path):
    process = subprocess.Popen(command, cwd=directory, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
    state = {"truncated": False, "error": None}

    def drain():
        retained = 0
        try:
            with log_path.open("xb") as output:
                while True:
                    chunk = process.stdout.read(65536)
                    if not chunk:
                        break
                    keep = min(len(chunk), LOG_LIMIT - retained)
                    output.write(chunk[:keep])
                    retained += keep
                    state["truncated"] |= keep != len(chunk)
        except Exception as error:
            state["error"] = str(error)

    reader = threading.Thread(target=drain, daemon=True)
    reader.start()
    try:
        result = process.wait(timeout=45)
    except subprocess.TimeoutExpired:
        process.kill()
        process.wait(timeout=10)
        raise
    finally:
        reader.join(timeout=5)
        if not reader.is_alive():
            process.stdout.close()
    require(not reader.is_alive() and state["error"] is None and not state["truncated"],
            "entry process output was not completely retained within bound")
    return result


def verify(directory, architecture, output):
    require(os.name == "nt", "native Windows fixture requires Windows")
    require(platform.machine().lower() in ({"arm64", "aarch64"} if architecture == "ARM64" else {"amd64", "x86_64"}),
            "fixture must run on its native architecture")
    directory = directory.resolve(strict=True)
    output = output.absolute()
    output.mkdir(parents=True, exist_ok=False)
    work = output / "working-copy"
    work.mkdir()
    evidence = output / "evidence"
    evidence.mkdir()
    module_path = Path(__file__).resolve().parents[1] / "RuntimeManifest/generate.py"
    spec = importlib.util.spec_from_file_location("client_manifest", module_path)
    manifest_tool = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(manifest_tool)
    with (directory / "client-build.json").open("rb") as stream:
        raw = stream.read(1024 * 1024 + 1)
    require(len(raw) <= 1024 * 1024, "build manifest exceeds bound")
    manifest = json.loads(raw)
    require(manifest.get("architecture") == architecture and manifest.get("protocol") == manifest_tool.PROTOCOL,
            "wrong build manifest architecture/protocol")
    expected = {row["name"]: row for row in manifest["files"]}
    require(set(expected) == {"WowClient64.exe", "XvidDecoder.dll"} and len(manifest["files"]) == 2,
            "unexpected client binary set")
    for name, dll in (("WowClient64.exe", False), ("XvidDecoder.dll", True)):
        before = manifest_tool.inspect_file(directory / name, architecture, dll)
        require(before == expected[name], "client binary does not match build manifest")
        shutil.copyfile(directory / name, work / name)
        require(manifest_tool.inspect_file(work / name, architecture, dll) == before, "client changed while copied")
    executable = str(work / "WowClient64.exe")
    result = dict(scope="native-windows-executable-entry-only", architecture=architecture,
                  sourceRevision=manifest["source_revision"], buildId=manifest["build_id"],
                  clientManifestSha256=hashlib.sha256(raw).hexdigest(), files=manifest["files"],
                  qualified=False, gameInitializationStarted=False, cases=[])
    try:
        for index, arguments in enumerate(((FLAG, "extra"), (FLAG + "=1",), (FLAG.upper(),))):
            code = run_bounded([executable, *arguments], work, evidence / f"invalid-{index}.log")
            result["cases"].append(dict(arguments=list(arguments), exitCode=code))
            require(code == 64 and not (work / RECEIPT).exists(), "malformed entry flag reached normal initialization")
        code = run_bounded([executable, FLAG], work, evidence / "entry.log")
        result["cases"].append(dict(arguments=[FLAG], exitCode=code))
        require(code == 0, "real client entry check failed")
        receipt, digest = read_receipt(work / RECEIPT, architecture)
        shutil.copyfile(work / RECEIPT, evidence / RECEIPT)
        result["entry"] = receipt
        result["receiptSha256"] = digest
        code = run_bounded([executable, FLAG], work, evidence / "existing-receipt.log")
        require(code == 66 and read_receipt(work / RECEIPT, architecture)[1] == digest,
                "repeat entry attempt replaced existing evidence")
        result["existingReceiptPreserved"] = True
        result["qualified"] = True
        return result
    finally:
        crash = work / "WowClient_crash.log"
        if crash.is_file():
            with crash.open("rb") as stream, (evidence / "last-entry-crash.log").open("xb") as target:
                target.write(stream.read(LOG_LIMIT))
        (evidence / "qualification.json").write_text(json.dumps(result, indent=2) + "\n", encoding="utf-8")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--directory", type=Path, required=True)
    parser.add_argument("--architecture", choices=("ARM64", "x64"), required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    result = verify(args.directory, args.architecture, args.output)
    print(json.dumps({key: result[key] for key in ("scope", "architecture", "qualified", "gameInitializationStarted")}))


if __name__ == "__main__":
    main()
