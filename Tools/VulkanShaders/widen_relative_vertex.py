r"""Widens the relative-addressed (skinning) vertex modules of an installed Vulkan shader set from 512 to 1024 rows.

translate_shaders.py now emits those modules as v[1024] (VCONST_XL). This applies the same change to the modules
already installed, without glslang: in each vertex record with `vconst=512` whose module bounds-checks an indexed
constant read against 512 (the dx_const helper), both the uniform array length and that bound become 1024, and the
manifest row says `vconst=1024`. Modules that only read direct rows stay at 512.

  python widen_relative_vertex.py --set "<client>/VulkanShadersRetail" --backup <folder>
"""
import argparse, re, shutil, struct
from pathlib import Path

OP_CONSTANT, OP_SLESS, OP_ULESS, OP_TYPE_ARRAY = 43, 177, 176, 28
OP_EXT_INST = 12
OP_LITERAL_ONLY = {3, 5, 6, 7, 71, 72}  # OpSource, OpName, OpMemberName, OpString, OpDecorate, OpMemberDecorate


def widen(words):
    """Returns the patched words, or None when the module has no indexed read against 512."""
    consts, i = {}, 5
    while i < len(words):
        n, op = words[i] >> 16, words[i] & 0xFFFF
        if n == 0:
            raise ValueError("zero-length instruction")
        if op == OP_CONSTANT and len(words) > i + 3 and words[i + 3] == 512 and n == 4:
            consts[words[i + 2]] = i + 3
        i += n
    if not consts:
        return None
    compared, other, i = False, [], 5
    while i < len(words):
        n, op = words[i] >> 16, words[i] & 0xFFFF
        # Names and decorations carry literals, not ids; an extended instruction's number is a literal too.
        if op not in OP_LITERAL_ONLY and op != OP_CONSTANT:
            operands = words[i + 5:i + n] if op == OP_EXT_INST else words[i + 1:i + n]
            used = [c for c in consts if c in operands]
            if used:
                if op in (OP_SLESS, OP_ULESS):
                    compared = True
                elif op != OP_TYPE_ARRAY:
                    other.append(op)
        i += n
    if not compared:
        return None
    if other:
        raise ValueError("512 is also used by op(s) %s" % sorted(set(other)))
    out = list(words)
    for at in consts.values():
        out[at] = 1024
    return out


def main():
    p = argparse.ArgumentParser()
    p.add_argument("--set", type=Path, required=True)
    p.add_argument("--backup", type=Path, required=True)
    a = p.parse_args()
    manifest = a.set / "manifest.txt"
    text = manifest.read_text(encoding="utf-8")
    blocks = re.split(r"(\n\s*\n)", text)
    a.backup.mkdir(parents=True, exist_ok=True)
    shutil.copy2(manifest, a.backup / "manifest.txt")
    done = skipped = refused = 0
    for k, block in enumerate(blocks):
        head = block.lstrip("\n").split("\n", 1)[0]
        if not head.startswith("record ") or " vertex " not in head or "vconst=512" not in head:
            continue
        spv = a.set / head.split()[4]
        raw = spv.read_bytes()
        try:
            patched = widen(struct.unpack("<%dI" % (len(raw) // 4), raw))
        except ValueError as e:
            print("refused", spv.name, e)
            refused += 1
            continue
        if patched is None:
            skipped += 1
            continue
        (a.backup / spv.parent.name).mkdir(exist_ok=True)
        shutil.copy2(spv, a.backup / spv.parent.name / spv.name)
        spv.write_bytes(struct.pack("<%dI" % len(patched), *patched))
        blocks[k] = block.replace("vconst=512", "vconst=1024", 1)
        done += 1
    manifest.write_text("".join(blocks), encoding="utf-8", newline="\n")
    print("widened %d vertex module(s), %d direct-read module(s) kept at 512, %d refused" % (done, skipped, refused))


main()
