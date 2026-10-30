#!/usr/bin/env python3
"""Inspect compiled PE files without loading them; write build-only evidence.

PE layout reference: https://learn.microsoft.com/windows/win32/debug/pe-format
This checks direct imports, not delay imports or the availability of Windows/Wine
system libraries. It does not establish that the client starts or plays correctly.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import stat
import struct
import tempfile

MACHINES = {"x64": 0x8664, "ARM64": 0xAA64}
PROTOCOL = {"build": 12340, "container_slots": 254, "quest_slots": 25, "max_class_id": 32}
MAX_IMPORTS = 4096
MAX_DESCRIPTOR_BYTES = 2 * 1024 * 1024
HASH_CHUNK = 1024 * 1024


class ManifestError(ValueError):
    """An input does not meet the compiled client artifact contract."""


def require(condition, message):
    if not condition:
        raise ManifestError(message)


class PEReader:
    def __init__(self, stream, size):
        self.stream = stream
        self.size = size
        self.regions = []

    def read(self, offset, length):
        require(0 <= offset <= self.size and 0 <= length <= self.size - offset,
                "truncated or out-of-bounds PE data")
        self.stream.seek(offset)
        data = self.stream.read(length)
        require(len(data) == length, "PE file changed or was truncated while reading")
        return data

    def mapped(self, rva, length):
        require(rva > 0 and length > 0 and rva + length <= 0x100000000,
                "invalid PE RVA range")
        for virtual, raw, raw_size in self.regions:
            if virtual <= rva and rva - virtual < raw_size:
                available = raw_size - (rva - virtual)
                require(length <= available, "PE RVA points outside file-backed section data")
                return raw + rva - virtual, available
        raise ManifestError("PE RVA is not backed by file data")

    def dll_name(self, rva):
        offset, available = self.mapped(rva, 1)
        data = self.read(offset, min(available, 261))
        end = data.find(b"\0")
        require(0 < end <= 260, "DLL import name is empty or unterminated")
        try:
            name = data[:end].decode("ascii")
        except UnicodeDecodeError as error:
            raise ManifestError("DLL import name is not ASCII") from error
        require(re.fullmatch(r"[A-Za-z0-9_+.-]+\.dll", name, re.IGNORECASE) is not None,
                "DLL import name is not a safe DLL basename")
        return name

    def inspect(self, architecture, expected_dll):
        dos = self.read(0, 64)
        require(dos[:2] == b"MZ", "missing MZ signature")
        pe_offset = struct.unpack_from("<I", dos, 60)[0]
        require(pe_offset >= 64, "PE header overlaps DOS header")
        header = self.read(pe_offset, 24)
        require(header[:4] == b"PE\0\0", "missing PE signature")
        machine, sections = struct.unpack_from("<HH", header, 4)
        optional_size, flags = struct.unpack_from("<HH", header, 20)
        require(machine == MACHINES[architecture],
                f"PE machine 0x{machine:04x} does not match {architecture}")
        require(flags & 0x0002, "PE is not an executable image")
        require(bool(flags & 0x2000) == expected_dll, "PE executable/DLL flag does not match filename")
        require(1 <= sections <= 96, "invalid PE section count")
        require(112 <= optional_size <= 4096, "invalid PE32+ optional header size")
        optional = self.read(pe_offset + 24, optional_size)
        require(struct.unpack_from("<H", optional)[0] == 0x20B, "PE32+ header required")
        image_size, headers_size = struct.unpack_from("<II", optional, 56)
        table = pe_offset + 24 + optional_size
        require(table + sections * 40 <= headers_size <= self.size,
                "invalid PE header/section table bounds")
        require(headers_size <= image_size, "PE image is smaller than its headers")
        directories = struct.unpack_from("<I", optional, 108)[0]
        require(directories <= (optional_size - 112) // 8, "PE directory table exceeds optional header")

        self.regions = [(0, 0, headers_size)]
        spans = [(0, headers_size)]
        for index in range(sections):
            section = self.read(table + index * 40, 40)
            virtual_size, virtual, raw_size, raw = struct.unpack_from("<IIII", section, 8)
            extent = max(virtual_size, raw_size)
            if extent:
                require(virtual >= headers_size and virtual + extent <= image_size,
                        "PE section exceeds image bounds")
                require(all(virtual + extent <= start or virtual >= end for start, end in spans),
                        "overlapping PE virtual sections")
                spans.append((virtual, virtual + extent))
            if raw_size:
                require(raw >= headers_size and raw <= self.size and raw_size <= self.size - raw,
                        "PE section exceeds file bounds")
                self.regions.append((virtual, raw, raw_size))

        imports = {}
        if directories >= 2:
            rva, size = struct.unpack_from("<II", optional, 120)
            require(bool(rva) == bool(size), "inconsistent PE import directory")
            if rva:
                require(20 <= size <= (MAX_IMPORTS + 1) * 20, "PE import directory exceeds bounded limit")
                offset, _ = self.mapped(rva, size)
                terminated = False
                for index in range(size // 20):
                    descriptor = struct.unpack("<IIIII", self.read(offset + index * 20, 20))
                    if not any(descriptor):
                        terminated = True
                        break
                    require(index < MAX_IMPORTS, "too many PE import descriptors")
                    name = self.dll_name(descriptor[3])
                    imports.setdefault(name.casefold(), name)
                require(terminated, "PE import descriptor table has no terminator")
        return {"machine": f"0x{machine:04x}", "format": "PE32+",
                "executable": True, "dll": expected_dll}, sorted(imports.values(), key=str.casefold)


def inspect_file(path, architecture, expected_dll):
    require(not path.is_symlink(), f"{path.name}: symbolic links are not runtime artifacts")
    with path.open("rb") as stream:
        before = os.fstat(stream.fileno())
        require(stat.S_ISREG(before.st_mode), f"{path.name}: regular file required")
        try:
            pe, imports = PEReader(stream, before.st_size).inspect(architecture, expected_dll)
        except ManifestError as error:
            raise ManifestError(f"{path.name}: {error}") from error
        digest = hashlib.sha256()
        stream.seek(0)
        total = 0
        while total < before.st_size:
            chunk = stream.read(min(HASH_CHUNK, before.st_size - total))
            require(bool(chunk), f"{path.name}: file shrank during hashing")
            digest.update(chunk)
            total += len(chunk)
        require(not stream.read(1), f"{path.name}: file grew during hashing")
        after = os.fstat(stream.fileno())
        require(total == before.st_size and
                (before.st_size, before.st_mtime_ns, before.st_ctime_ns) ==
                (after.st_size, after.st_mtime_ns, after.st_ctime_ns),
                f"{path.name}: file changed during inspection")
    return {"name": path.name, "size": total, "sha256": digest.hexdigest(), "pe": pe, "imports": imports}


def descriptor_info(path):
    with path.open("rb") as stream:
        data = stream.read(MAX_DESCRIPTOR_BYTES + 1)
    require(len(data) <= MAX_DESCRIPTOR_BYTES, "descriptor manifest exceeds bounded limit")
    try:
        manifest = json.loads(data)
    except (ValueError, UnicodeError, RecursionError) as error:
        raise ManifestError("invalid descriptor manifest JSON") from error
    require(isinstance(manifest, dict), "descriptor manifest must be an object")
    for key, expected in PROTOCOL.items():
        require(type(manifest.get(key)) is int and manifest[key] == expected,
                f"descriptor manifest {key} must equal {expected}")
    return {"name": path.name, "size": len(data), "sha256": hashlib.sha256(data).hexdigest()}


def generate(directory, architecture, revision, build_id, descriptor_manifest):
    require(architecture in MACHINES, "architecture must be x64 or ARM64")
    require(re.fullmatch(r"[0-9a-fA-F]{40}", revision) is not None, "revision must be a full 40-digit commit SHA")
    require(0 < len(build_id) <= 128 and all(32 <= ord(char) <= 126 for char in build_id),
            "build ID must contain 1..128 printable ASCII characters")
    directory = Path(directory)
    descriptor = descriptor_info(Path(descriptor_manifest))
    files = [inspect_file(directory / name, architecture, dll)
             for name, dll in (("WowClient64.exe", False), ("XvidDecoder.dll", True))]
    require("xviddecoder.dll" in {name.casefold() for name in files[0]["imports"]},
            "WowClient64.exe does not import its packaged XvidDecoder.dll")
    manifest = {"schema_version": 1, "component": "esteria-reconstructed-client",
                "evidence": "build-only", "architecture": architecture,
                "source_revision": revision.lower(), "build_id": build_id,
                "protocol": dict(PROTOCOL), "descriptor_manifest": descriptor, "files": files}
    temporary = None
    try:
        with tempfile.NamedTemporaryFile(mode="w", encoding="utf-8", newline="\n",
                                         dir=directory, prefix=".client-build-", suffix=".tmp", delete=False) as output:
            temporary = Path(output.name)
            json.dump(manifest, output, indent=2, sort_keys=True)
            output.write("\n")
            output.flush()
            os.fsync(output.fileno())
        os.replace(temporary, directory / "client-build.json")
    finally:
        if temporary is not None:
            temporary.unlink(missing_ok=True)
    return manifest


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--directory", type=Path, required=True)
    parser.add_argument("--architecture", choices=MACHINES, required=True)
    parser.add_argument("--revision", required=True)
    parser.add_argument("--build-id", required=True)
    parser.add_argument("--descriptor-manifest", type=Path, required=True)
    args = parser.parse_args()
    try:
        generate(args.directory, args.architecture, args.revision, args.build_id, args.descriptor_manifest)
    except (OSError, ManifestError) as error:
        parser.exit(1, f"runtime manifest: {error}\n")
    print(args.directory / "client-build.json")


if __name__ == "__main__":
    main()
