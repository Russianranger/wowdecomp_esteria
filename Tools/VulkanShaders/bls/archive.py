"""The client's shader containers: the archives in load order, the `(listfile)` inventory, the
profile fallback the engine applies, and every record it loads.

The engine (CGxDevice::IShaderLoad) opens `Shaders\\<Vertex|Pixel>\\<profile>\\<name>.bls` for the
profile its caps report and falls back one profile at a time when the file is missing or bad:
vertex vs_3_0 -> vs_2_0 -> vs_1_1, pixel ps_3_0 -> ps_2_0 -> ps_1_4 -> ps_1_1. The Vulkan device
reports vs_3_0 / ps_3_0, so that is the chain walked here.

A `.bls` container: `uint32 magic 'GXSH'`, `uint32 version 0x10003`, `uint32 permutationCount`, then
that many records of `uint32 a; uint32 b; uint16 c; uint16 d; uint32 size; uint8 code[size]`, each
padded to 4 bytes. The record's `code` is what the engine keeps in CGxShader::m_code; its md5 is
the manifest's key.
"""
import collections
import hashlib
import os
from pathlib import Path
import re
import struct
import sys

from . import dx9ir as ir
from . import mpq_extract as mq

BS = chr(92)
VS_FALLBACK = ['vs_3_0', 'vs_2_0', 'vs_1_1']
PS_FALLBACK = ['ps_3_0', 'ps_2_0', 'ps_1_4', 'ps_1_1']


# Client.cpp::g_localeNames/CheckAvailableLocales rotation includes aliases
# which this translator does not yet have a qualified MPQ hash locale for.
CLIENT_LOCALES = ('deDE', 'enGB', 'enUS', 'esES', 'frFR', 'koKR', 'zhCN', 'zhTW', 'enCN', 'enTW', 'esMX', 'ruRU')

LOCALES = {'enUS': 0x409, 'enGB': 0x809, 'koKR': 0x412, 'frFR': 0x40c,
           'deDE': 0x407, 'zhCN': 0x804, 'zhTW': 0x404, 'esES': 0x40a,
           'esMX': 0x80a, 'ruRU': 0x419}


def directory_entries(folder):
    """Windows names are case insensitive; refuse ambiguous Linux imports."""
    found = {}
    for path in Path(folder).iterdir():
        key = path.name.lower()
        if key in found:
            raise ValueError('case-colliding providers: %s and %s' % (found[key], path))
        found[key] = path
    return found


def available_locale(found, locale):
    """Match Client.cpp's regular locale-<name>.MPQ availability check.

    Imported paths are case insensitive like Windows; unsafe links and case
    collisions remain refusals, and no archive payload is read at this stage.
    """
    folder = found.get(locale.lower())
    if folder is None:
        return False
    if folder.is_symlink():
        raise ValueError('unsafe locale directory: %s' % folder)
    if not folder.is_dir():
        return False
    archive = directory_entries(folder).get('locale-%s.mpq' % locale.lower())
    if archive is None:
        return False
    if archive.is_symlink():
        raise ValueError('unsafe locale archive: %s' % archive)
    return archive.is_file()


def select_locale(datadir, explicit=None):
    found = directory_entries(datadir)
    if explicit is not None:
        matches = [name for name in CLIENT_LOCALES if name.lower() == explicit.lower()]
        if not matches:
            raise ValueError('unsupported locale: %s' % explicit)
        start = CLIENT_LOCALES.index(matches[0])
        for offset in range(len(CLIENT_LOCALES)):
            effective = CLIENT_LOCALES[(start + offset) % len(CLIENT_LOCALES)]
            if available_locale(found, effective):
                if effective not in LOCALES:
                    raise ValueError('effective client locale %s is unsupported by this translator' % effective)
                return effective
        raise ValueError('no available classical locale archive for requested locale %s' % matches[0])
    # The installed client's configured locale takes precedence over directory guesses.
    root = Path(datadir).parent
    wtf = directory_entries(root).get('wtf')
    if wtf and wtf.is_dir():
        config = directory_entries(wtf).get('config.wtf')
        if config and config.is_file():
            with config.open(encoding='utf-8-sig') as stream:
                for line in stream:
                    match = re.fullmatch(r'\s*SET\s+locale\s+"([a-zA-Z]{4})"\s*', line, re.I)
                    if match:
                        return select_locale(datadir, match.group(1))
    locales = [name for name in CLIENT_LOCALES if available_locale(found, name)]
    if len(locales) > 1:
        raise ValueError('multiple locales: pass --locale matching the client Config.wtf')
    if locales:
        return select_locale(datadir, locales[0])
    # Root-only diagnostic extraction remains useful without an explicit locale.
    # A requested or configured locale never silently degrades to that profile.
    return None


def provider_paths(datadir, locale=None):
    """Normal complete-client MPQ providers, lowest to highest actual engine priority.

    Client.cpp OpenArchives assigns *descending* priority to the base table. PatchFiles.cpp
    appends the two plain patches after sorting named root/locale paths descending, then
    OpenArchives opens that list backwards with increasing priority. Do not substitute
    numeric patch ordering or the ordering of a particular stock installation.
    """
    data = Path(datadir)
    found = directory_entries(data)
    if 'common.mpq' not in found:
        raise ValueError('normal client data requires common.MPQ: %s' % data)
    if 'alternate.mpq' in found:
        raise ValueError('alternate.MPQ archive views are unsupported by this translator')
    locale = select_locale(data, locale)
    local_dir = found.get(locale.lower()) if locale else None
    local = directory_entries(local_dir) if local_dir and local_dir.is_dir() else {}
    # Exact relative order of the mask-2/mask-3 base entries in Client.cpp:s_archives.
    # common-3 was appended to retain engine indices; it has the lowest base priority.
    high_to_low = [found.get(n) for n in ('expansion.mpq', 'lichking.mpq', 'common.mpq', 'common-2.mpq')]
    if locale:
        high_to_low += [local.get(n % locale.lower()) for n in
                        ('locale-%s.mpq', 'speech-%s.mpq', 'expansion-locale-%s.mpq',
                         'lichking-locale-%s.mpq', 'expansion-speech-%s.mpq', 'lichking-speech-%s.mpq')]
    high_to_low.append(found.get('common-3.mpq'))
    result = [p for p in reversed(high_to_low) if p is not None]
    # Reverse of the plain-patch append order: locale first, then root patch.MPQ.
    if locale and 'patch-%s.mpq' % locale.lower() in local:
        result.append(local['patch-%s.mpq' % locale.lower()])
    if 'patch.mpq' in found:
        result.append(found['patch.mpq'])
    named = [p for n, p in found.items() if n.startswith('patch-') and n.endswith('.mpq')]
    if locale:
        prefix = 'patch-%s-' % locale.lower()
        named += [p for n, p in local.items() if n.startswith(prefix) and n.endswith('.mpq')]
    result.extend(sorted(named, key=lambda p: str(p.relative_to(data)).replace('/', BS).lower()))
    for path in result:
        if path.is_symlink() or not (path.is_file() or path.is_dir()):
            raise ValueError('unsafe archive provider: %s' % path)
    return result


class FolderArchive:
    """An engine folder-MPQ provider; inventory only its shader subtree."""
    def __init__(self, path):
        self.files = {}
        root = directory_entries(path).get('shaders')
        if root:
            if root.is_symlink() or not root.is_dir():
                raise ValueError('unsafe folder shader root: %s' % root)
            for directory, dirs, files in os.walk(root, followlinks=False):
                entries = directory_entries(directory)
                for child in entries.values():
                    if child.is_symlink():
                        raise ValueError('symlink in folder shader provider: %s' % child)
                    if not (child.is_file() or child.is_dir()):
                        raise ValueError('non-regular file in folder shader provider: %s' % child)
                for name in files:
                    child = entries[name.lower()]
                    relative = str(child.relative_to(path)).replace('/', BS)
                    self.files[relative.lower()] = (relative, child)

    def find(self, name):
        return 0 if name.lower() == '(listfile)' or name.lower() in self.files else None

    def read_file(self, name):
        if name.lower() == '(listfile)':
            return '\n'.join(item[0] for item in self.files.values()).encode('utf-8'), {}
        path = self.files[name.lower()][1]
        if path.stat().st_size > mq.MAX_FILE_SIZE:
            raise ValueError('shader file too large: %s' % path)
        return path.read_bytes(), {}

    def close(self):
        pass


def archives(datadir, locale=None):
    """Every effective provider; unreadable winners fail instead of exposing stale shaders."""
    locale = select_locale(datadir, locale)
    out = []
    try:
        for path in provider_paths(datadir, locale):
            provider = FolderArchive(path) if path.is_dir() else mq.MPQ(path, LOCALES.get(locale, 0))
            out.append((str(path.relative_to(datadir)), provider))
            if provider.find('(listfile)') is None:
                raise ValueError('shader inventory requires (listfile) in %s' % path)
        return out
    except Exception:
        for _, provider in out:
            provider.close()
        raise


def grab(arch, path):
    """The bytes of `path` from the last archive that holds it (the load order's winner)."""
    found = None
    for n, m in arch:
        if m.find(path) is not None:
            found = (n, m)
    if not found:
        return None, None
    n, m = found
    data, _ = m.read_file(path)
    return n, data


def split(data, strict=False):
    """The records of one .bls container."""
    magic, ver, nperm = struct.unpack_from('<III', data, 0)
    if strict and (magic != 0x47585348 or ver != 0x10003 or not 0 < nperm <= (len(data) - 12) // 16):
        raise ValueError('invalid or empty shader container header')
    recs, off = [], 12
    for i in range(nperm):
        a, b, c, d, size = struct.unpack_from('<IIHHI', data, off)
        off += 16
        if strict and (size < 8 or size % 4 or size > len(data) - off):
            raise ValueError('invalid shader record size')
        code = data[off:off + size]
        off += size
        off = (off + 3) & ~3
        recs.append(dict(i=i, a=a, b=b, c=c, d=d, size=size, code=code))
    if strict and off != len(data):
        raise ValueError('shader container has unaccounted bytes')
    return magic, ver, nperm, recs, off


def entries(arch, prefix='shaders'):
    """Every listed path under `prefix`, across the whole load order, case-insensitively."""
    out = set()
    for n, m in arch:
        if m.find('(listfile)') is None:
            continue
        data, _ = m.read_file('(listfile)')
        for ln in data.decode('ascii', 'replace').replace('\r\n', '\n').split('\n'):
            ln = ln.strip()
            if ln.lower().startswith(prefix.lower()):
                out.add(ln)
    return sorted(out)


def inventory(arch):
    """{(kind, profile_lower): {name_lower: name_as_listed}} from the listfile."""
    inv = collections.defaultdict(dict)
    for n in entries(arch, prefix='shaders'):
        p = n.split(BS)
        if len(p) == 4 and p[1].lower() in ('vertex', 'pixel') and p[3].lower().endswith('.bls'):
            inv[(p[1].lower(), p[2].lower())][p[3][:-4].lower()] = p[3][:-4]
    return inv


def resolve(inv):
    """Which container the engine loads per (kind, name) under the 3_0 caps: [(kind, profile, name)]."""
    out = []
    for kind, chain in (('vertex', VS_FALLBACK), ('pixel', PS_FALLBACK)):
        names = {}
        for prof in chain:
            for low, name in inv.get((kind, prof), {}).items():
                names.setdefault(low, name)
        for low in sorted(names):
            for prof in chain:
                if low in inv.get((kind, prof), {}):
                    out.append((kind, prof, inv[(kind, prof)][low]))
                    break
    return out


class Record(object):
    __slots__ = ('kind', 'profile', 'name', 'perm', 'code', 'md5', 'sh', 'names', 'hdr')

    @property
    def key(self):
        return '%s:%d' % (self.name, self.perm)


def enumerate_records(datadir, locale=None, strict=False):
    """Every record the engine loads, resolved through its fallback chain (an empty or missing
    container falls back one profile, as the engine does). Byte-identical records are separate
    entries with the same md5; the caller folds them."""
    arch = archives(datadir, locale)
    try:
        return _enumerate_records(arch, strict)
    finally:
        for _, provider in arch:
            provider.close()


def _enumerate_records(arch, strict=False):
    inv = inventory(arch)
    out = []
    for kind, prof, name in resolve(inv):
        chain = VS_FALLBACK if kind == 'vertex' else PS_FALLBACK
        data = None
        for prof2 in chain[chain.index(prof):]:
            low = name.lower()
            if low not in inv.get((kind, prof2), {}):
                continue
            path = BS.join(['Shaders', kind.capitalize(), prof2, inv[(kind, prof2)][low] + '.bls'])
            src, data = grab(arch, path)
            if data is None or len(data) < 12:
                if strict:
                    raise ValueError('selected shader container is missing or truncated: %s' % path)
                data = None
                continue
            prof = prof2
            break
        if data is None:
            if strict:
                raise ValueError('no usable shader container for %s:%s' % (kind, name))
            continue
        magic, ver, nperm, records, consumed = split(data, strict)
        for rec in records:
            r = Record()
            r.kind, r.profile, r.name, r.perm = kind, prof, name, rec['i']
            r.code = rec['code']
            r.md5 = hashlib.md5(r.code).hexdigest()
            r.hdr = (rec['a'], rec['b'], rec['c'], rec['d'])
            try:
                r.sh = ir.decode_strict(r.code) if strict else ir.decode(r.code)
            except ValueError:
                r.sh = None       # an SM1 stream: not translated
            out.append(r)
    return out


def extra_vertex_records(folder, strict=False):
    """Vertex records read from raw vs_3_0 token streams (<folder>/*.bin), named after the file."""
    out = []
    for fn in sorted(os.listdir(folder)):
        if not fn.lower().endswith('.bin'):
            continue
        r = Record()
        r.kind, r.profile, r.name, r.perm = 'vertex', 'vs_3_0', os.path.splitext(fn)[0], 0
        with open(os.path.join(folder, fn), 'rb') as f:
            r.code = f.read(mq.MAX_FILE_SIZE + 1) if strict else f.read()
        if strict and len(r.code) > mq.MAX_FILE_SIZE:
            raise ValueError('extra vertex shader exceeds file bound')
        r.md5 = hashlib.md5(r.code).hexdigest()
        r.hdr = (0, 0, 0, 0)
        try:
            r.sh = ir.decode_strict(r.code) if strict else ir.decode(r.code)
        except ValueError:
            r.sh = None
        out.append(r)
    return out


def extra_pixel_records(folder, strict=False):
    """Pixel records read from raw ps_3_0 token streams (<folder>/*.bin), named after the file."""
    out = []
    for fn in sorted(os.listdir(folder)):
        if not fn.lower().endswith('.bin'):
            continue
        r = Record()
        r.kind, r.profile, r.name, r.perm = 'pixel', 'ps_3_0', os.path.splitext(fn)[0], 0
        with open(os.path.join(folder, fn), 'rb') as f:
            r.code = f.read(mq.MAX_FILE_SIZE + 1) if strict else f.read()
        if strict and len(r.code) > mq.MAX_FILE_SIZE:
            raise ValueError('extra pixel shader exceeds file bound')
        r.md5 = hashlib.md5(r.code).hexdigest()
        r.hdr = (0, 0, 0, 0)
        try:
            r.sh = ir.decode_strict(r.code) if strict else ir.decode(r.code)
        except ValueError:
            r.sh = None
        out.append(r)
    return out
