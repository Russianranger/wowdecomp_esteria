"""The translation run: every record the engine loads, folded by md5, emitted as GLSL, compiled to
SPIR-V, and the manifest the device reads.

    translate.py --data <client Data folder> --glslang <glslang.exe> --out <tree>
    translate.py ... --names UI:0,Combiners_Mod:4 --print   # look at records, write nothing
    translate.py ... --no-compile                           # emit GLSL only

The output tree: <out>/vertex/<md5>.{glsl,spv,spvasm}, <out>/pixel/<md5>.{glsl,spv,spvasm},
<out>/manifest.txt (the device's contract) and <out>/index.txt (one line per engine name, for
people). The run wipes vertex/ and pixel/ first: the tree is always one run's output.
"""
import argparse
import collections
import hashlib
import os
import shutil
import signal
import struct
import subprocess
import sys
import tempfile
import threading
import time

from . import archive
from .emitter import (Emitter, Refused, companion_module, companion_block, manifest_block, FOG_ROW,
                      PCONST, VCONST_SMALL)


COMPILER_TIMEOUT = 30.0
COMPILER_OUTPUT_LIMIT = 4 * 1024 * 1024
SPIRV_SIZE_LIMIT = 16 * 1024 * 1024


def compile_glsl(glslang, glsl_path, spv_path, stage, *, timeout=COMPILER_TIMEOUT,
                 output_limit=COMPILER_OUTPUT_LIMIT):
    """Bounded real compiler/validator/disassembler; returns (ok, spvasm, log).

    Both output streams share one cap and are drained while the child runs. A
    runaway compiler fails instead of blocking setup or retaining unlimited text.
    The caller owns the staged output path; a stale module cannot satisfy success.
    """
    if not 0 < timeout <= 120 or not 0 < output_limit <= 16 * 1024 * 1024:
        raise ValueError('compiler execution bounds are invalid')
    if os.path.lexists(spv_path):
        os.remove(spv_path)
    cmd = [glslang, '-V', '--target-env', 'vulkan1.0', '--spirv-val', '--spirv-dis', '-S', stage,
           '-o', spv_path, glsl_path]
    captured = bytearray()
    done, overflow, stop = threading.Event(), threading.Event(), threading.Event()
    errors = []
    try:
        child = subprocess.Popen(cmd, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, bufsize=0,
                                 start_new_session=os.name != 'nt')
    except OSError as error:
        return False, '', 'compiler could not start: %s' % error

    def drain():
        try:
            while not stop.is_set():
                chunk = os.read(child.stdout.fileno(), 65536)
                if not chunk:
                    break
                keep = chunk[:max(0, output_limit - len(captured))]
                captured.extend(keep)
                if len(keep) != len(chunk):
                    overflow.set()
        except OSError:
            errors.append('compiler diagnostic pipe failed')
        finally:
            done.set()

    reader = threading.Thread(target=drain, daemon=True)
    reader.start()
    deadline = time.monotonic() + timeout
    failure = None
    code = None
    try:
        # Do not reap the leader until its output is drained, so a timed-out
        # POSIX process group retains the identity we created for this compiler.
        while not done.wait(min(0.05, max(0, deadline - time.monotonic()))):
            if overflow.is_set():
                failure = 'compiler output limit exceeded';break
            if time.monotonic() >= deadline:
                failure = 'compiler execution timed out';break
        if not failure:
            if overflow.is_set():
                failure = 'compiler output limit exceeded'
            elif errors:
                failure = errors[0]
            else:
                if os.name != 'nt':
                    # Retain the exited leader until group cleanup, including
                    # children which closed both diagnostic pipes before it.
                    while not os.waitid(os.P_PID, child.pid, os.WEXITED | os.WNOWAIT | os.WNOHANG):
                        if time.monotonic() >= deadline:
                            failure = 'compiler execution timed out';break
                        time.sleep(min(0.01, max(0, deadline - time.monotonic())))
                else:
                    try:
                        code = child.wait(timeout=max(0.001, deadline - time.monotonic()))
                    except subprocess.TimeoutExpired:
                        failure = 'compiler execution timed out'
    finally:
        if os.name != 'nt' or child.returncode is None:
            try:
                if os.name != 'nt':
                    os.killpg(child.pid, signal.SIGKILL)
                else:
                    child.kill()
            except ProcessLookupError:
                pass
            code = child.wait(timeout=5)
        stop.set()
        reader.join(timeout=1)
        if reader.is_alive():
            failure = failure or 'compiler diagnostic pipe did not close'
        child.stdout.close()
    out = captured.decode('utf-8', errors='replace').replace('\r\n', '\n')
    lines = out.split('\n')
    if lines and lines[0].strip() == os.path.basename(glsl_path):
        lines = lines[1:]
    dis = '\n'.join(lines)
    module_valid = False
    if not failure and code == 0 and not os.path.islink(spv_path) and os.path.isfile(spv_path):
        with open(spv_path, 'rb') as module:
            size = os.fstat(module.fileno()).st_size
            module_valid = 20 <= size <= SPIRV_SIZE_LIMIT and size % 4 == 0 and module.read(4) == b'\x03\x02\x23\x07'
    ok = not failure and code == 0 and module_valid and '; SPIR-V' in dis
    if not ok and os.path.lexists(spv_path):
        os.remove(spv_path)
    return ok, dis, ((failure + '\n') if failure else '') + out.strip()


MANIFEST_HEAD = [
    '# Translated shader manifest -- generated by Tools/VulkanShaders (translate_shaders.py)',
    '# One block per byte-distinct record, blank-line separated; `#` starts a comment.',
    '# record <md5> <vertex|pixel> <profile> <spv path> vconst=<64|512|1024> pconst=20',
    '# name <Name:perm>        every engine name whose bytes have this md5; bare <Name> for record 0',
    '# sampler <slot> <2d|cube>   pixel: combined image sampler at set 0, binding <slot>',
    '# input <semantic> <vN>      vertex: attribute semantic the module declares (location per semantic)',
    '# output <semantic>          vertex: semantics written (position0, color0.., texcoord0.., fog0, psize0)',
    '# reads <semantic>           pixel: semantics read',
    '# flag fixed_fog             vertex (vs_2_0): writes oFog, the fixed-function fog FACTOR, as fog0;',
    '#                            pixel (ff): blends mix(p[%d].rgb, rgb, fog0) while p[%d].w > 0' % (FOG_ROW, FOG_ROW),
    '# flag point_size            vertex (vs_2_0): writes gl_PointSize from oPts; draw as a point list',
    '# flag point_sprite          pixel (ff): samples stage 0 at gl_PointCoord, reads no texcoord',
    '# companion <md5>            vertex: the `ff` pixel record to bind when the draw has no pixel shader',
    '# md5 = MD5 of the record\'s `size` bytes after its 16-byte .bls record header: the DX9',
    '# token stream from the version token through the END token, no padding (CGxShader::m_code).',
    '# An `ff` record has no bytecode: its md5 is of its own GLSL text.',
    '# Uniform block (set 1, binding 0, std140): { vec4 p[16]; vec4 misc; vec4 v[VCONST]; }',
    '# p[i] at byte 16*i, misc at 256, v[i] at 272 + 16*i; slice = 272 + 16*VCONST bytes.',
    '# Pixel modules declare v[64]; the vertex module\'s vconst sizes the pair\'s slice.',
    '# p[%d] = (fogColor.rgb / 255, fogOn ? 1 : 0), written by the device on every draw: no' % FOG_ROW,
    '# shipped pixel record reads c13..c15.',
    '']


def main(argv):
    ap = argparse.ArgumentParser(description=__doc__.split('\n\n')[0])
    ap.add_argument('--data', required=True, help="the client's Data folder (the one holding common.MPQ)")
    ap.add_argument('--locale', help='active client locale; inferred from Config.wtf or a single locale folder')
    ap.add_argument('--out', default=None, help='output tree')
    ap.add_argument('--names', default=None, help='only these Name:perm (comma-separated)')
    ap.add_argument('--print', action='store_true', help='print the GLSL of each emitted record')
    ap.add_argument('--glslang', default=None, help='glslang.exe (also $GLSLANG)')
    ap.add_argument('--extra-pixel', default=None, help='folder of raw ps_3_0 token streams (*.bin) to translate as extra pixel records, e.g. the native Retail glow permutations')
    ap.add_argument('--extra-vertex', default=None, help='folder of raw vs_3_0 token streams (*.bin) to translate as extra vertex records')
    ap.add_argument('--no-compile', action='store_true', help='emit GLSL only')
    ap.add_argument('--strict', action='store_true', help='require every record and both stages to compile; never publish a partial manifest')
    args = ap.parse_args(argv)

    if args.strict and (args.no_compile or args.names):
        print('--strict requires the complete compiled shader set; omit --names/--no-compile', file=sys.stderr)
        return 2

    glslang = None if args.no_compile else (args.glslang or os.environ.get('GLSLANG') or shutil.which('glslang')
                                             or shutil.which('glslangValidator'))
    if not args.no_compile and (glslang is None or not os.path.exists(glslang)):
        print('no glslang.exe found (--glslang PATH or $GLSLANG)', file=sys.stderr)
        return 2

    only = set(args.names.split(',')) if args.names else None
    out = args.out
    scratch = None
    if out is None:
        scratch = tempfile.mkdtemp(prefix='translate-')
        out = scratch
    write_tree = scratch is None

    try:
        recs = archive.enumerate_records(args.data, args.locale, strict=args.strict)
        if args.extra_pixel:
            recs += archive.extra_pixel_records(args.extra_pixel, strict=args.strict)
        if args.extra_vertex:
            recs += archive.extra_vertex_records(args.extra_vertex, strict=args.strict)
        if args.strict:
            if not recs or {r.kind for r in recs} != {'vertex', 'pixel'}:
                raise ValueError('strict setup requires nonempty vertex and pixel record sets')
            invalid = [r for r in recs if r.sh is None or r.sh.kind != {'vertex': 'vs', 'pixel': 'ps'}[r.kind]]
            if invalid:
                raise ValueError('strict setup refuses %d undecodable or mismatched-stage record(s): %s' %
                                 (len(invalid), ', '.join(r.key for r in invalid[:8])))
    except (OSError, ValueError, RuntimeError, struct.error, IndexError) as error:
        print('shader input validation failed: %s' % error, file=sys.stderr)
        if scratch:
            shutil.rmtree(scratch, ignore_errors=True)
        return 2
    # Fold byte-identical records: one module per md5, every Name:perm that has it listed.
    by_md5 = collections.OrderedDict()
    names = collections.defaultdict(list)
    for r in recs:
        if r.sh is None:
            continue
        names[r.md5].append(r.key)
        if r.perm == 0:
            names[r.md5].append(r.name)      # the bare name the engine interns for record 0
        by_md5.setdefault(r.md5, r)

    selected = collections.OrderedDict()
    for r in recs:
        if r.sh is None:
            continue
        if only is not None and r.key not in only and r.name not in only:
            continue
        selected.setdefault(r.md5, r)

    if write_tree:
        if args.strict:
            for name in ('manifest.txt', 'index.txt'):
                path = os.path.join(out, name)
                if os.path.lexists(path):
                    os.remove(path)
        for kind in ('vertex', 'pixel'):
            d = os.path.join(out, kind)
            if os.path.isdir(d):
                shutil.rmtree(d)
    for kind in ('vertex', 'pixel'):
        os.makedirs(os.path.join(out, kind), exist_ok=True)

    def write_module(kind, md5, glsl):
        """Writes <kind>/<md5>.glsl, compiles it to .spv and keeps the .spvasm; returns (status, log)."""
        kind_dir = os.path.join(out, kind)
        glsl_path = os.path.join(kind_dir, md5 + '.glsl')
        spv_path = os.path.join(kind_dir, md5 + '.spv')
        with open(glsl_path, 'w', encoding='utf-8', newline='\n') as f:
            f.write(glsl)
        if args.no_compile:
            return 'emitted', ''
        ok, dis, log = compile_glsl(glslang, glsl_path, spv_path, 'vert' if kind == 'vertex' else 'frag')
        if not ok:
            for p in (glsl_path if not args.print else None, spv_path):
                if p and os.path.exists(p):
                    os.remove(p)
            return 'failed', log.replace('\n', ' | ')[:400]
        with open(os.path.join(kind_dir, md5 + '.spvasm'), 'w', encoding='utf-8', newline='\n') as f:
            f.write(dis)
        return 'validated', ''

    results = []          # (rec, status, reason, em)
    blocks = []
    index_lines = []
    companions = collections.OrderedDict()    # md5 of the text -> [meta, [names], status, log]
    for md5, rec in selected.items():
        rec.names = names[md5]
        em = Emitter(rec)
        try:
            glsl = em.run()
        except Refused as e:
            results.append((rec, 'refused', str(e), em))
            continue
        if args.print:
            print(glsl)
        status, log = write_module(rec.kind, md5, glsl)
        if status == 'failed':
            results.append((rec, 'failed', log, em))
            continue
        results.append((rec, status, '', em))
        # The fixed-function pixel stage a flagged vs_2_0 record is drawn with (no pixel shader
        # exists for it): generated, compiled and folded by the md5 of its own text.
        comp_md5 = None
        if rec.kind == 'vertex' and em.flags:
            ctext, meta = companion_module(em, rec)
            comp_md5 = hashlib.md5(ctext.encode('utf-8')).hexdigest()
            if comp_md5 not in companions:
                if args.print:
                    print(ctext)
                cstatus, clog = write_module('pixel', comp_md5, ctext)
                companions[comp_md5] = [meta, [], cstatus, clog]
            companions[comp_md5][1].extend(n + '@ff' for n in rec.names)
            if companions[comp_md5][2] == 'failed':
                comp_md5 = None
        blocks.append(manifest_block(em, rec, '%s/%s.spv' % (rec.kind, md5), comp_md5))
        for n in rec.names:
            if ':' in n:
                extra = (' '.join('s%d:%s' % kv for kv in sorted(em.samplers.items())) or '-') \
                    if rec.kind == 'pixel' else ' '.join('%s=%s' % (s, r) for r, s in em.inputs.items())
                if comp_md5:
                    extra += ' companion=%s' % comp_md5
                index_lines.append('%-36s %s %-6s vconst=%-3d %s' % (n, md5, rec.kind, em.vconst, extra))
    comp_blocks = []
    for cmd5, (meta, cnames, cstatus, clog) in companions.items():
        if cstatus == 'failed':
            print('FAILED companion %s: %s' % (' = '.join(cnames), clog), file=sys.stderr)
            continue
        comp_blocks.append(companion_block(cmd5, meta, cnames, 'pixel/%s.spv' % cmd5))
        for n in cnames:
            if ':' in n:
                index_lines.append('%-36s %s %-6s vconst=%-3d %s' % (n, cmd5, 'pixel', VCONST_SMALL,
                                   's0:2d ' + ' '.join(sorted(meta['flags']))))
    if comp_blocks:
        blocks.append('# The fixed-function pixel stages generated for the vs_2_0 records above that carry a\n'
                      '# `companion` line (no pixel shader exists for them; profile `ff`, no bytecode, md5 of\n'
                      '# the module text). Bound when the draw has no pixel shader.\n' + comp_blocks[0])
        blocks.extend(comp_blocks[1:])

    refused = [(r, why) for r, status, why, em in results if status not in ('validated', 'emitted')]
    head = list(MANIFEST_HEAD)
    if refused:
        head.append('# Refused (not translated):')
        for r, why in refused:
            head.append('#   %s %s: %s' % (r.kind, ' = '.join(r.names), why))
        head.append('')
    failed = any(st == 'failed' or (args.strict and st == 'refused') for _, st, _, _ in results) or \
             any(c[2] == 'failed' for c in companions.values())
    if write_tree and not (args.strict and failed):
        with open(os.path.join(out, 'manifest.txt'), 'w', encoding='utf-8', newline='\n') as f:
            f.write('\n'.join(head) + '\n\n'.join(blocks) + '\n')
        with open(os.path.join(out, 'index.txt'), 'w', encoding='utf-8', newline='\n') as f:
            f.write('# Name:perm -> md5 kind vconst samplers/inputs (one line per engine name)\n')
            f.write('\n'.join(sorted(index_lines)) + '\n')
            if refused:
                f.write('\n# refused\n')
                for r, why in refused:
                    f.write('%-36s REFUSED: %s\n' % (' = '.join(r.names), why))

    counts = collections.Counter(status for _, status, _, _ in results)
    print('records: %d total, %d decodable, %d distinct; %d selected: %s' %
          (len(recs), sum(1 for r in recs if r.sh is not None), len(by_md5), len(selected),
           ', '.join('%s %d' % kv for kv in sorted(counts.items()))))
    reasons = collections.Counter()
    for r, status, why, em in results:
        if status in ('refused', 'failed'):
            reasons[(status, why.split(' (')[0][:90])] += 1
    for (status, why), n in reasons.most_common():
        print('  %s x%d: %s' % (status, n, why))
    for r, status, why, em in results:
        if status == 'failed':
            print('FAILED %s: %s' % (' = '.join(r.names), why), file=sys.stderr)
    if companions:
        print('companions (fixed-function pixel stages for the flagged vs_2_0 records): %d generated, %s' %
              (len(companions), ', '.join('%s %d' % kv for kv in sorted(
                  collections.Counter(c[2] for c in companions.values()).items()))))
    if write_tree:
        print(('partial diagnostics only; no manifest published: %s' if args.strict and failed else 'wrote %s') % out)
    elif scratch:
        shutil.rmtree(scratch, ignore_errors=True)
    return 1 if failed else 0


if __name__ == '__main__':
    sys.exit(main(sys.argv[1:]))
