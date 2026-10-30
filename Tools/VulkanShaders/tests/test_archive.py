"""Synthetic MPQs exercise actual table decoding and provider precedence without game assets."""
import pathlib
import struct
import sys
import tempfile
import unittest
import zlib

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parents[1]))
from bls import archive, mpq_extract as mq


def encrypt(data, key):
    result = []
    seed = 0xEEEEEEEE
    aligned = len(data) // 4 * 4
    for value in struct.unpack('<%dI' % (len(data) // 4), data[:aligned]):
        seed = (seed + mq.CT[0x400 + (key & 0xFF)]) & 0xFFFFFFFF
        result.append(value ^ ((key + seed) & 0xFFFFFFFF))
        key = ((((~key) << 0x15) + 0x11111111) | (key >> 0x0B)) & 0xFFFFFFFF
        seed = (value + seed + (seed << 5) + 3) & 0xFFFFFFFF
    return struct.pack('<%dI' % len(result), *result) + data[aligned:]


def make_mpq(path, files, listfile=True, compressed=False):
    """files: [(name, bytes, locale)]. Emit a minimal genuine format-0 MPQ."""
    path.parent.mkdir(parents=True, exist_ok=True)
    records = list(files)
    if listfile:
        records.append(('(listfile)', '\n'.join(n for n, _, _ in files).encode('ascii'), 0))
    hashes = [(0xFFFFFFFF, 0xFFFFFFFF, 0xFFFF, 0xFFFF, 0xFFFFFFFF)] * 32
    blocks, payload = [], bytearray()
    header_size = 44 if compressed else 32
    for number, (name, data, locale) in enumerate(records):
        index = mq.hash_string(name, 0) % len(hashes)
        while hashes[index][-1] != 0xFFFFFFFF:
            index = (index + 1) % len(hashes)
        hashes[index] = (mq.hash_string(name, 1), mq.hash_string(name, 2), locale, 0, number)
        blocks.append((header_size + len(payload), len(data), len(data), mq.FLAG_EXISTS | mq.FLAG_SINGLE_UNIT))
        payload.extend(data)
    hash_bytes = b''.join(struct.pack('<IIHHI', *item) for item in hashes)
    block_bytes = b''.join(struct.pack('<IIII', *item) for item in blocks)
    if compressed:
        hash_bytes = b'\x02' + zlib.compress(hash_bytes)
        packed_block = b'\x02' + zlib.compress(block_bytes)
        if len(packed_block) < len(block_bytes):
            block_bytes = packed_block
    hash_start = header_size + len(payload)
    block_start = hash_start + len(hash_bytes)
    total = block_start + len(block_bytes)
    header = struct.pack('<4sIIHHIIII', b'MPQ\x1a', header_size, 0 if compressed else total, int(compressed), 3,
                         hash_start, block_start, len(hashes), len(blocks))
    if compressed:
        # The low archive size is intentionally stale; format1 must ignore it.
        header += struct.pack('<QHH', 0, 0, 0)
    path.write_bytes(header + payload + encrypt(hash_bytes, mq.hash_string('(hash table)', 3))
                     + encrypt(block_bytes, mq.hash_string('(block table)', 3)))


class ArchiveTests(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self.tmp.cleanup)
        self.data = pathlib.Path(self.tmp.name) / 'Data'
        self.data.mkdir()
        self.shader = r'Shaders\Vertex\vs_3_0\Test.bls'
        self.put('common.MPQ', b'common')

    def put(self, name, data):
        make_mpq(self.data / name, [(self.shader, data, 0)])

    def providers(self, locale=None):
        providers = archive.archives(self.data, locale)
        self.addCleanup(lambda: [provider.close() for _, provider in providers])
        return providers

    def test_arbitrary_suffix_case_and_locale_order(self):
        self.put('patch.MPQ', b'plain')
        self.put('enUS/patch-enUS.MPQ', b'local-plain')
        self.put('enUS/patch-enUS-Z.MPQ', b'local-named')
        self.put('patch-Z.MPQ', b'Z')
        self.put('pAtCh-ZzzzzMountStudio-001.mPq', b'custom')
        providers = self.providers('enUS')
        self.assertEqual(archive.grab(providers, self.shader)[1], b'custom')
        self.assertEqual([p.name for p in archive.provider_paths(self.data, 'enUS')],
                         ['common.MPQ', 'patch-enUS.MPQ', 'patch.MPQ', 'patch-enUS-Z.MPQ',
                          'patch-Z.MPQ', 'pAtCh-ZzzzzMountStudio-001.mPq'])

    def test_base_priority_matches_engine_not_chronological_install(self):
        self.put('common-3.MPQ', b'common3')
        self.put('lichking.MPQ', b'lichking')
        self.put('expansion.MPQ', b'expansion')
        self.put('enUS/locale-enUS.MPQ', b'locale')
        self.assertEqual(archive.grab(self.providers(), self.shader)[1], b'expansion')

    def test_folder_patch_can_override_archive_and_add_shader(self):
        folder = self.data / 'patch-ZZZ.MPQ' / 'Shaders' / 'Vertex' / 'vs_3_0'
        folder.mkdir(parents=True)
        (folder / 'Test.bls').write_bytes(b'folder')
        (folder / 'Added.bls').write_bytes(b'new')
        providers = self.providers()
        self.assertEqual(archive.grab(providers, self.shader)[1], b'folder')
        self.assertIn(r'Shaders\Vertex\vs_3_0\Added.bls', archive.entries(providers))

    def test_explicit_locale_and_neutral_fallback(self):
        make_mpq(self.data / 'patch-Z.MPQ', [(self.shader, b'neutral', 0),
                                           (self.shader, b'french', 0x40c)])
        self.assertEqual(archive.grab(self.providers('frFR'), self.shader)[1], b'french')
        self.assertEqual(archive.grab(self.providers('enUS'), self.shader)[1], b'neutral')

    def test_format1_compressed_tables_and_deprecated_archive_size(self):
        make_mpq(self.data / 'patch-Z.MPQ', [(self.shader, b'format1 shader', 0)], compressed=True)
        self.assertEqual(archive.grab(self.providers(), self.shader)[1], b'format1 shader')

    def test_ambiguous_locale_rejected_unless_configured(self):
        (self.data / 'enUS').mkdir()
        (self.data / 'frFR').mkdir()
        with self.assertRaisesRegex(ValueError, 'multiple locales'):
            archive.provider_paths(self.data)
        wtf = self.data.parent / 'WTF'
        wtf.mkdir()
        (wtf / 'Config.wtf').write_text('SET locale "frFR"\n')
        self.assertEqual(archive.select_locale(self.data), 'frFR')

    def test_corrupt_winner_is_not_silently_skipped(self):
        (self.data / 'patch-Z.MPQ').write_bytes(b'bad archive')
        with self.assertRaisesRegex(RuntimeError, 'no MPQ header'):
            archive.archives(self.data)

    def test_case_collision_rejected(self):
        self.put('COMMON.mpq', b'collision')
        with self.assertRaisesRegex(ValueError, 'case-colliding'):
            archive.archives(self.data)

    def test_missing_listfile_rejects_incomplete_inventory(self):
        make_mpq(self.data / 'patch-Z.MPQ', [(self.shader, b'hidden', 0)], listfile=False)
        with self.assertRaisesRegex(ValueError, 'requires .listfile.'):
            archive.archives(self.data)

    def test_malformed_userdata_does_not_loop(self):
        (self.data / 'patch-Z.MPQ').write_bytes(struct.pack('<4sIII', b'MPQ\x1b', 0, 0, 16))
        with self.assertRaisesRegex(ValueError, 'user-data offset'):
            archive.archives(self.data)

    def test_symlink_provider_rejected(self):
        try:
            (self.data / 'patch-Z.MPQ').symlink_to(self.data / 'common.MPQ')
        except OSError:
            self.skipTest('symlinks unavailable')
        with self.assertRaisesRegex(ValueError, 'unsafe archive provider'):
            archive.archives(self.data)

    def test_compressed_sector_rejects_size_lies(self):
        compressed = b'\x02' + zlib.compress(b'a' * 8192)
        with self.assertRaisesRegex(ValueError, 'declared size'):
            mq.decompress_sector(compressed, 32)
        self.assertEqual(mq.decompress_sector(compressed, 8192), b'a' * 8192)

    def test_truncated_mpq_header_is_clear_failure(self):
        (self.data / 'patch-Z.MPQ').write_bytes(b'MPQ\x1a' + b'\0' * 8)
        with self.assertRaisesRegex(ValueError, 'truncated MPQ data'):
            archive.archives(self.data)


if __name__ == '__main__':
    unittest.main()
