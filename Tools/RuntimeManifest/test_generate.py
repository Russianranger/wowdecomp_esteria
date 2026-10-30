import hashlib
import io
import json
from pathlib import Path
import struct
import tempfile
import unittest
from unittest import mock

import generate


def pe_image(architecture="ARM64", dll=False, imports=()):
    """One file-backed section with an import table and separate name strings."""
    data = bytearray(0x600)
    data[:2] = b"MZ"
    struct.pack_into("<I", data, 0x3C, 0x80)
    data[0x80:0x84] = b"PE\0\0"
    struct.pack_into("<HHIIIHH", data, 0x84, generate.MACHINES[architecture], 1,
                     0, 0, 0, 0xF0, 0x0002 | (0x2000 if dll else 0))
    struct.pack_into("<H", data, 0x98, 0x20B)
    struct.pack_into("<II", data, 0x98 + 56, 0x2000, 0x200)
    struct.pack_into("<I", data, 0x98 + 108, 16)
    data[0x188:0x190] = b".rdata\0\0"
    struct.pack_into("<IIII", data, 0x190, 0x400, 0x1000, 0x400, 0x200)
    if imports:
        struct.pack_into("<II", data, 0x98 + 120, 0x1000, (len(imports) + 1) * 20)
        name_offset = 0x300
        for index, name in enumerate(imports):
            raw = name.encode("ascii") + b"\0"
            struct.pack_into("<IIIII", data, 0x200 + 20 * index,
                             0, 0, 0, 0x1000 + name_offset - 0x200, 0x1200)
            data[name_offset:name_offset + len(raw)] = raw
            name_offset += len(raw)
    return data


class PEValidationTests(unittest.TestCase):
    def inspect(self, data, architecture="ARM64", dll=False):
        return generate.PEReader(io.BytesIO(data), len(data)).inspect(architecture, dll)

    def test_both_architectures_and_direct_imports(self):
        for architecture in generate.MACHINES:
            with self.subTest(architecture=architecture):
                pe, imports = self.inspect(pe_image(architecture, imports=("XvidDecoder.dll", "KERNEL32.dll")), architecture)
                self.assertEqual(imports, ["KERNEL32.dll", "XvidDecoder.dll"])
                self.assertEqual(pe["machine"], f"0x{generate.MACHINES[architecture]:04x}")
                self.assertEqual(pe["format"], "PE32+")
                self.assertTrue(pe["executable"])
                self.assertFalse(pe["dll"])
                self.assertTrue(self.inspect(pe_image(architecture, dll=True), architecture, True)[0]["dll"])

    def test_mismatched_architecture(self):
        for source, expected in (("ARM64", "x64"), ("x64", "ARM64")):
            with self.subTest(source=source), self.assertRaisesRegex(generate.ManifestError, "machine"):
                self.inspect(pe_image(source), expected)

    def test_truncated_headers_and_section_data(self):
        image = pe_image(imports=("KERNEL32.dll",))
        for length in (0, 1, 63, 64, 127, 151, 0x187, 0x1AF, 0x200, 0x5FF):
            with self.subTest(length=length), self.assertRaises(generate.ManifestError):
                self.inspect(image[:length])

    def test_signatures_and_flags(self):
        for offset, value, message in ((0, b"NO", "MZ"), (0x80, b"NOPE", "PE signature"),
                                       (0x98, b"\x0b\x01", "PE32\\+")):
            data = pe_image()
            data[offset:offset + len(value)] = value
            with self.subTest(offset=offset), self.assertRaisesRegex(generate.ManifestError, message):
                self.inspect(data)
        for flags, dll in ((0, False), (0x2000, True), (2, True), (0x2002, False)):
            data = pe_image()
            struct.pack_into("<H", data, 0x96, flags)
            with self.subTest(flags=flags, dll=dll), self.assertRaises(generate.ManifestError):
                self.inspect(data, dll=dll)

    def test_import_directory_rva_and_size_bounds(self):
        for rva, size in ((0x90000000, 40), (0x13F0, 40), (0x1000, 0),
                          (0, 40), (0x1000, 19), (0x1000, 20 * 4098)):
            data = pe_image(imports=("KERNEL32.dll",))
            struct.pack_into("<II", data, 0x98 + 120, rva, size)
            with self.subTest(rva=rva, size=size), self.assertRaises(generate.ManifestError):
                self.inspect(data)

    def test_import_descriptor_termination_and_name_rva(self):
        data = pe_image(imports=("KERNEL32.dll",))
        struct.pack_into("<I", data, 0x98 + 124, 20)
        with self.assertRaisesRegex(generate.ManifestError, "terminator"):
            self.inspect(data)
        for name_rva in (0, 0x2000, 0xFFFFFFFF):
            data = pe_image(imports=("KERNEL32.dll",))
            struct.pack_into("<I", data, 0x20C, name_rva)
            with self.subTest(rva=name_rva), self.assertRaises(generate.ManifestError):
                self.inspect(data)

    def test_virtual_zero_fill_is_not_file_backed(self):
        data = pe_image(imports=("KERNEL32.dll",))
        struct.pack_into("<I", data, 0x190, 0x800) # More virtual than raw bytes.
        struct.pack_into("<I", data, 0x20C, 0x1500)
        with self.assertRaisesRegex(generate.ManifestError, "not backed"):
            self.inspect(data)

    def test_dll_names_are_bounded_ascii_basenames(self):
        for name in (b"", b"\xff.dll", b"../outside.dll", b"C:\\evil.dll", b"x.exe", b"x" * 261):
            data = pe_image(imports=("KERNEL32.dll",))
            data[0x300:0x300 + len(name) + 1] = name + b"\0"
            with self.subTest(name=name[:20]), self.assertRaises(generate.ManifestError):
                self.inspect(data)
        data = pe_image(imports=("KERNEL32.dll",))
        data[0x300:] = b"A" * 0x300
        with self.assertRaisesRegex(generate.ManifestError, "unterminated"):
            self.inspect(data)

    def test_optional_directory_and_section_bounds(self):
        for offset, value in ((0x98 + 108, 17), (0x98 + 60, 0x100),
                               (0x19C, 0x590), (0x194, 0x100)):
            data = pe_image()
            struct.pack_into("<I", data, offset, value)
            with self.subTest(offset=offset), self.assertRaises(generate.ManifestError):
                self.inspect(data)


class ManifestTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        self.directory = Path(self.temporary.name)
        self.descriptors = self.directory / "limits.json"
        self.descriptors.write_text(json.dumps(generate.PROTOCOL), encoding="utf-8")
        (self.directory / "WowClient64.exe").write_bytes(pe_image(imports=("XvidDecoder.dll", "KERNEL32.dll")))
        (self.directory / "XvidDecoder.dll").write_bytes(pe_image(dll=True, imports=("ucrtbase.dll",)))

    def run_generator(self, **overrides):
        options = dict(directory=self.directory, architecture="ARM64", revision="ABCDEF1234" * 4,
                       build_id="ci-42-attempt-1", descriptor_manifest=self.descriptors)
        options.update(overrides)
        return generate.generate(**options)

    def test_manifest_hashes_revision_and_build_only_evidence(self):
        manifest = self.run_generator()
        output = (self.directory / "client-build.json").read_bytes()
        self.assertEqual(json.loads(output), manifest)
        self.assertEqual(manifest["source_revision"], "abcdef1234" * 4)
        self.assertEqual(manifest["evidence"], "build-only")
        self.assertEqual(manifest["protocol"], generate.PROTOCOL)
        self.assertEqual(manifest["descriptor_manifest"]["sha256"], hashlib.sha256(self.descriptors.read_bytes()).hexdigest())
        for record in manifest["files"]:
            data = (self.directory / record["name"]).read_bytes()
            self.assertEqual(record["sha256"], hashlib.sha256(data).hexdigest())
            self.assertEqual(record["size"], len(data))
        self.run_generator()
        self.assertEqual((self.directory / "client-build.json").read_bytes(), output)

    def test_missing_or_wrong_machine_codec_preserves_existing_manifest(self):
        output = self.directory / "client-build.json"
        output.write_bytes(b"previous-manifest")
        codec = self.directory / "XvidDecoder.dll"
        codec.unlink()
        with self.assertRaises(FileNotFoundError):
            self.run_generator()
        codec.write_bytes(pe_image("x64", dll=True))
        with self.assertRaisesRegex(generate.ManifestError, "machine"):
            self.run_generator()
        self.assertEqual(output.read_bytes(), b"previous-manifest")
        self.assertEqual(list(self.directory.glob(".client-build-*.tmp")), [])

    def test_codec_must_be_a_direct_client_import(self):
        (self.directory / "WowClient64.exe").write_bytes(pe_image(imports=("KERNEL32.dll",)))
        with self.assertRaisesRegex(generate.ManifestError, "does not import"):
            self.run_generator()

    def test_descriptor_layout_must_match(self):
        for key in generate.PROTOCOL:
            data = dict(generate.PROTOCOL)
            data[key] += 1
            self.descriptors.write_text(json.dumps(data), encoding="utf-8")
            with self.subTest(key=key), self.assertRaisesRegex(generate.ManifestError, key):
                self.run_generator()

    def test_descriptor_json_is_bounded(self):
        for data in (b"[]", b"{", b" " * (generate.MAX_DESCRIPTOR_BYTES + 1)):
            self.descriptors.write_bytes(data)
            with self.assertRaises(generate.ManifestError):
                self.run_generator()

    def test_revision_and_build_id_validation(self):
        for options in ({"revision": "1234567"}, {"revision": "g" * 40},
                        {"build_id": ""}, {"build_id": "line\nbreak"}, {"build_id": "a" * 129}):
            with self.subTest(options=options), self.assertRaises(generate.ManifestError):
                self.run_generator(**options)

    def test_failed_atomic_replace_keeps_previous_manifest_and_cleans_temp(self):
        output = self.directory / "client-build.json"
        output.write_bytes(b"previous-manifest")
        with mock.patch.object(generate.os, "replace", side_effect=OSError("disk error")):
            with self.assertRaises(OSError):
                self.run_generator()
        self.assertEqual(output.read_bytes(), b"previous-manifest")
        self.assertEqual(list(self.directory.glob(".client-build-*.tmp")), [])

    def test_hashing_uses_multiple_bounded_reads(self):
        image = pe_image(dll=True)
        path = self.directory / "XvidDecoder.dll"
        path.write_bytes(image + b"overlay" * (generate.HASH_CHUNK // 7 + 1))
        result = generate.inspect_file(path, "ARM64", True)
        self.assertEqual(result["sha256"], hashlib.sha256(path.read_bytes()).hexdigest())


if __name__ == "__main__":
    unittest.main()
