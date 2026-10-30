"""A minimal MPQ reader: the hash and block tables, the file lookup, sector decompression (zlib, bzip2).

Enough to read the client's `.bls` shader containers and `(listfile)` out of its archives, in the
client's own load order (a later archive wins).
"""
import bz2
import os
import struct
import zlib


def make_crypt_table():
    t = [0] * 0x500
    seed = 0x00100001
    for i in range(0x100):
        idx = i
        for _ in range(5):
            seed = (seed * 125 + 3) % 0x2AAAAB
            t1 = (seed & 0xFFFF) << 0x10
            seed = (seed * 125 + 3) % 0x2AAAAB
            t2 = (seed & 0xFFFF)
            t[idx] = (t1 | t2)
            idx += 0x100
    return t


CT = make_crypt_table()


def hash_string(s, htype):
    seed1 = 0x7FED7FED
    seed2 = 0xEEEEEEEE
    for ch in s.upper():
        c = ord(ch)
        seed1 = (CT[(htype * 0x100) + c] ^ ((seed1 + seed2) & 0xFFFFFFFF)) & 0xFFFFFFFF
        seed2 = (c + seed1 + seed2 + (seed2 << 5) + 3) & 0xFFFFFFFF
    return seed1


def decrypt(data, key):
    n = len(data) // 4
    words = list(struct.unpack('<%dI' % n, data[:n * 4]))
    seed = 0xEEEEEEEE
    key = key & 0xFFFFFFFF
    for i in range(n):
        seed = (seed + CT[0x400 + (key & 0xFF)]) & 0xFFFFFFFF
        ch = (words[i] ^ ((key + seed) & 0xFFFFFFFF)) & 0xFFFFFFFF
        words[i] = ch
        key = ((((~key) << 0x15) + 0x11111111) | (key >> 0x0B)) & 0xFFFFFFFF
        seed = (ch + seed + (seed << 5) + 3) & 0xFFFFFFFF
    return struct.pack('<%dI' % n, *words) + data[n * 4:]


FLAG_EXISTS = 0x80000000
FLAG_SINGLE_UNIT = 0x01000000
FLAG_IMPLODE = 0x00000100
FLAG_COMPRESS = 0x00000200
FLAG_ENCRYPTED = 0x00010000
FLAG_FIX_KEY = 0x00020000
FLAG_SECTOR_CRC = 0x04000000
MAX_FILE_SIZE = 64 * 1024 * 1024  # this reader extracts shaders/listfiles, not bulk game assets
MAX_TABLE_ENTRIES = 1 << 20


class MPQ:
    def __init__(self, path, locale=0):
        self.path = path
        self.locale = locale
        self.f = open(path, 'rb')
        try:
            self._find_header()
            self._read_tables()
        except Exception:
            self.f.close()
            raise

    def close(self):
        self.f.close()

    def _read_exact(self, size):
        data = self.f.read(size)
        if len(data) != size:
            raise ValueError('truncated MPQ data: %s' % self.path)
        return data

    def _find_header(self):
        f = self.f
        size = os.path.getsize(self.path)
        self.physical_size = size
        off = 0
        while off < size:
            f.seek(off)
            magic = f.read(4)
            if magic == b'MPQ\x1a':
                self.archive_offset = off
                hdr = self._read_exact(0x20 - 4)
                (self.header_size, self.archive_size, self.format_version,
                 self.block_size_exp, self.hash_table_pos, self.block_table_pos,
                 self.hash_table_size, self.block_table_size) = struct.unpack('<IIHHIIII', hdr)
                if self.format_version not in (0, 1) or self.header_size < (44 if self.format_version == 1 else 32):
                    raise ValueError('only classic MPQ formats 0/1 are supported: %s' % self.path)
                if self.format_version == 1:
                    high_block, hash_high, block_high = struct.unpack('<QHH', self._read_exact(12))
                    if high_block or hash_high or block_high:
                        raise ValueError('MPQ offsets above 32 bits are unsupported: %s' % self.path)
                if self.archive_size < self.header_size or off + self.archive_size > size:
                    raise ValueError('truncated MPQ archive: %s' % self.path)
                if self.block_size_exp > 16:
                    raise ValueError('invalid MPQ sector size: %s' % self.path)
                return
            elif magic == b'MPQ\x1b':
                ud = self._read_exact(12)
                user_data_size, hdr_off, ud_hdr_size = struct.unpack('<III', ud)
                if hdr_off < 16 or off + hdr_off >= size:
                    raise ValueError('invalid MPQ user-data offset: %s' % self.path)
                off = off + hdr_off
                continue
            else:
                off += 0x200
        raise RuntimeError('no MPQ header in %s' % self.path)

    def _read_tables(self):
        f = self.f
        for pos, count in ((self.hash_table_pos, self.hash_table_size),
                           (self.block_table_pos, self.block_table_size)):
            if count > MAX_TABLE_ENTRIES or pos < self.header_size or pos + count * 16 > self.archive_size:
                raise ValueError('invalid or oversized MPQ table: %s' % self.path)
        if not self.hash_table_size:
            raise ValueError('empty MPQ hash table: %s' % self.path)
        f.seek(self.archive_offset + self.hash_table_pos)
        raw = self._read_exact(self.hash_table_size * 16)
        raw = decrypt(raw, hash_string('(hash table)', 3))
        self.hash_table = [struct.unpack_from('<IIHHI', raw, i * 16) for i in range(self.hash_table_size)]
        f.seek(self.archive_offset + self.block_table_pos)
        raw = self._read_exact(self.block_table_size * 16)
        raw = decrypt(raw, hash_string('(block table)', 3))
        self.block_table = [struct.unpack_from('<IIII', raw, i * 16) for i in range(self.block_table_size)]

    def find(self, name):
        idx = hash_string(name, 0) % self.hash_table_size
        a = hash_string(name, 1)
        b = hash_string(name, 2)
        neutral = None
        for i in range(self.hash_table_size):
            e = self.hash_table[(idx + i) % self.hash_table_size]
            na, nb, loc, plat, blk = e
            if blk == 0xFFFFFFFF:
                break
            if na == a and nb == b and blk != 0xFFFFFFFE:
                if blk >= len(self.block_table):
                    raise ValueError('invalid MPQ block index: %s' % self.path)
                if loc == self.locale:
                    return blk
                if loc == 0:
                    neutral = blk
        return neutral

    def read_file(self, name):
        blk = self.find(name)
        if blk is None:
            return None
        file_pos, cmp_size, file_size, flags = self.block_table[blk]
        if not (flags & FLAG_EXISTS):
            raise ValueError('MPQ hash references an absent block: %s' % self.path)
        if flags & 0x02000000:
            raise ValueError('MPQ delete markers are unsupported: %s' % self.path)
        if file_size > MAX_FILE_SIZE or file_pos + cmp_size > self.archive_size:
            raise ValueError('oversized or truncated MPQ shader/listfile: %s' % self.path)
        base = self.archive_offset + file_pos
        sector_size = 512 << self.block_size_exp
        key = None
        if flags & FLAG_ENCRYPTED:
            bn = name.split('\\')[-1]
            key = hash_string(bn, 3)
            if flags & FLAG_FIX_KEY:
                key = ((key + file_pos) ^ file_size) & 0xFFFFFFFF
        info = dict(flags=hex(flags), cmp_size=cmp_size, file_size=file_size,
                    sector_size=sector_size, encrypted=bool(flags & FLAG_ENCRYPTED),
                    single_unit=bool(flags & FLAG_SINGLE_UNIT))
        self.f.seek(base)
        if flags & FLAG_SINGLE_UNIT:
            data = self._read_exact(cmp_size)
            if key is not None:
                data = decrypt(data, key)
            if (flags & (FLAG_COMPRESS | FLAG_IMPLODE)) and cmp_size < file_size:
                data = decompress_sector(data, file_size)
            if len(data) != file_size:
                raise ValueError('MPQ file length mismatch: %s' % self.path)
            return data[:file_size], info
        nsect = (file_size + sector_size - 1) // sector_size
        if flags & (FLAG_COMPRESS | FLAG_IMPLODE):
            ntbl = nsect + 1
            if flags & FLAG_SECTOR_CRC:
                ntbl += 1
            if ntbl * 4 > cmp_size:
                raise ValueError('truncated MPQ sector table: %s' % self.path)
            raw = self._read_exact(ntbl * 4)
            if key is not None:
                raw = decrypt(raw, (key - 1) & 0xFFFFFFFF)
            offs = list(struct.unpack('<%dI' % ntbl, raw))
            if offs[0] < ntbl * 4 or any(a > b for a, b in zip(offs, offs[1:])) or offs[-1] > cmp_size:
                raise ValueError('invalid MPQ sector offsets: %s' % self.path)
            out = bytearray()
            for i in range(nsect):
                self.f.seek(base + offs[i])
                chunk = self._read_exact(offs[i + 1] - offs[i])
                if key is not None:
                    chunk = decrypt(chunk, (key + i) & 0xFFFFFFFF)
                want = min(sector_size, file_size - len(out))
                if len(chunk) == want:
                    out += chunk
                else:
                    out += decompress_sector(chunk, want)
            return bytes(out), info
        else:
            if cmp_size != file_size:
                raise ValueError('uncompressed MPQ length mismatch: %s' % self.path)
            data = self._read_exact(file_size)
            if key is not None:
                data = decrypt(data, key)
            return data, info


def decompress_sector(chunk, want):
    if not chunk:
        raise ValueError('empty compressed MPQ sector')
    mask = chunk[0]
    body = chunk[1:]
    if mask == 0x02:
        decoder = zlib.decompressobj()
        data = decoder.decompress(body, want + 1)
    elif mask == 0x10:
        decoder = bz2.BZ2Decompressor()
        data = decoder.decompress(body, max_length=want + 1)
    elif mask == 0x08:
        raise RuntimeError('PKWARE implode not implemented')
    elif mask == 0x01:
        raise RuntimeError('huffman not implemented')
    else:
        raise RuntimeError('unknown compression mask 0x%02x' % mask)
    if len(data) != want or not decoder.eof or decoder.unused_data:
        raise ValueError('compressed MPQ sector does not match declared size')
    return data


# Effective provider order is calculated by archive.provider_paths from Client.cpp/PatchFiles.cpp.
