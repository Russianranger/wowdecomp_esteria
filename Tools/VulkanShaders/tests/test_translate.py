"""Real child supervision and synthetic DX9 inputs; no game-data qualification."""
import contextlib
import io
import os
from pathlib import Path
import shutil
import struct
import sys
import tempfile
import time
import unittest
from unittest import mock

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from bls import archive, dx9ir, translate
import translate_shaders


def tokens(*words):
    return struct.pack('<%dI' % len(words), *words)


VERTEX = tokens(0xfffe0200, 0x0200001f, 0x80000000, 0x900f0000,
                0x02000001, 0xc00f0000, 0x90e40000, 0xffff)
PIXEL = tokens(0xffff0200, 0x02000001, 0x800f0800, 0xa0e40000, 0xffff)
WEATHER = tokens(0xfffe0200, 0x0200001f, 0x80000000, 0x900f0000,
                 0x02000001, 0xc00f0000, 0x90e40000,
                 0x02000001, 0xc0010001, 0xa0000000, 0xffff)
REFUSED = tokens(0xfffe0200, 0x0000dead, 0xffff)


def container(code):
    return struct.pack('<IIII IHHI', 0x47585348, 0x10003, 1, 0, 0, 0, 0, len(code)) + code


class Fixture(unittest.TestCase):
    def setUp(self):
        temporary = tempfile.TemporaryDirectory()
        self.addCleanup(temporary.cleanup)
        self.root = Path(temporary.name)
        self.data = self.root / 'Data'
        (self.data / 'common.MPQ').mkdir(parents=True)
        self.output = self.root / 'output'

    def put(self, kind, code, name='Fixture'):
        path = self.data / 'common.MPQ' / 'Shaders' / kind.capitalize() / ('vs_2_0' if kind == 'vertex' else 'ps_2_0')
        path.mkdir(parents=True, exist_ok=True)
        target = path / (name + '.bls');target.write_bytes(container(code));return target

    def run_translation(self, strict=True, extra=(), compiler=None):
        arguments = ['--data', str(self.data), '--out', str(self.output), '--glslang', compiler or sys.executable]
        if strict:arguments.append('--strict')
        log=io.StringIO()
        with contextlib.redirect_stdout(log), contextlib.redirect_stderr(log):
            result=translate.main(arguments + list(extra))
        self.translation_log=log.getvalue()
        return result


class StrictTranslation(Fixture):
    def test_empty_missing_stage_and_undecodable_inputs_preserve_old_output(self):
        self.output.mkdir();sentinel=self.output / 'manifest.txt';sentinel.write_text('old generation')
        self.assertEqual(self.run_translation(), 2)
        self.put('vertex', VERTEX)
        self.assertEqual(self.run_translation(), 2)
        self.put('pixel', tokens(0xffff0101, 0xffff))
        self.assertEqual(self.run_translation(), 2)
        self.assertEqual(sentinel.read_text(), 'old generation')

    def test_strict_rejects_partial_selection_and_uncompiled_modes(self):
        for flags in (['--names', 'Fixture'], ['--no-compile']):
            self.assertEqual(self.run_translation(extra=flags), 2)

    def test_refused_emitter_is_failure_only_in_strict_mode_and_no_manifest(self):
        self.put('vertex', REFUSED);self.put('pixel', PIXEL)
        with mock.patch.object(translate, 'compile_glsl', return_value=(True, '; SPIR-V', '')):
            self.assertEqual(self.run_translation(strict=False), 0)
            self.assertIn('# Refused', (self.output / 'manifest.txt').read_text())
            self.assertEqual(self.run_translation(), 1)
            self.assertFalse((self.output / 'manifest.txt').exists())
            self.assertFalse((self.output / 'index.txt').exists())

    def test_companion_compiler_failure_cannot_publish_partial_manifest(self):
        self.put('vertex', WEATHER);self.put('pixel', PIXEL)
        refused=[]
        def compile_module(compiler, source, output, stage):
            if 'fixed-function pixel stage' in Path(source).read_text():
                refused.append(source);return False, '', 'controlled companion rejection'
            return True, '; SPIR-V', ''
        with mock.patch.object(translate, 'compile_glsl', side_effect=compile_module):
            self.assertEqual(self.run_translation(), 1)
        self.assertFalse((self.output / 'manifest.txt').exists())
        self.assertEqual(len(refused),1)

    def test_strict_container_token_bounds_and_stage_identity(self):
        path=self.put('vertex', VERTEX);self.put('pixel', PIXEL)
        for bad in (b'bad', container(VERTEX)[:-1], container(VERTEX) + b'pad!',
                    container(tokens(0xfffe0200, 0x02000001, 0xc00f0000)),
                    container(tokens(0xfffe0200, 0x0200fffe, 0xffff)),
                    container(tokens(0xfffe0200, 0xffff, 0)), container(PIXEL)):
            with self.subTest(size=len(bad)):
                path.write_bytes(bad);self.assertEqual(self.run_translation(), 2)
        with self.assertRaises(ValueError):dx9ir.decode_strict(tokens(0xfffe0200, 0x02000001, 0xc00f0000, 0x90e40000))

    def test_outer_cli_propagates_strict_and_does_not_deploy_failure(self):
        with mock.patch.object(translate, 'main', return_value=1) as implementation, \
                mock.patch.object(translate_shaders, 'deploy') as deploy, \
                contextlib.redirect_stdout(io.StringIO()), contextlib.redirect_stderr(io.StringIO()):
            result=translate_shaders.main(['--client', str(self.data), '--out', str(self.output),
                '--glslang', sys.executable, '--strict', '--deploy'])
        self.assertEqual(result, 1);self.assertIn('--strict', implementation.call_args.args[0]);deploy.assert_not_called()


@unittest.skipIf(os.name == 'nt', 'supervisor helper uses a POSIX executable script; real compiler case is cross-platform')
class CompilerSupervision(Fixture):
    def compiler(self, code):
        path=self.root/'compiler'
        path.write_text('#!'+sys.executable+'\nimport os,sys,time,struct\n'+code+'\n');path.chmod(0o700)
        return str(path)

    def compile(self, code, **limits):
        source=self.root/'fixture.vert';source.write_text('#version 450\nvoid main(){}\n')
        target=self.root/'fixture.spv';target.write_bytes(b'stale-output')
        result=translate.compile_glsl(self.compiler(code), str(source), str(target), 'vert', **limits)
        return result, target

    def test_success_retains_required_flags_and_new_module(self):
        (ok, assembly, log), output=self.compile("assert sys.argv[1:6]==['-V','--target-env','vulkan1.0','--spirv-val','--spirv-dis']\n"
            "open(sys.argv[sys.argv.index('-o')+1],'wb').write(struct.pack('<5I',0x07230203,0x10000,0,1,0))\n"
            "print('; SPIR-V\\nOpMemoryModel Logical GLSL450')")
        self.assertTrue(ok);self.assertIn('OpMemoryModel',assembly);self.assertEqual(output.stat().st_size,20)

    def test_nonzero_exit_and_missing_module_reject_stale_output(self):
        for code in ("print('compiler error',file=sys.stderr);sys.exit(2)", "print('; SPIR-V')"):
            (ok, _, _), output=self.compile(code)
            self.assertFalse(ok);self.assertFalse(output.exists())

    def test_timeout_stops_child_and_removes_partial_output(self):
        started=time.monotonic()
        (ok, _, log), output=self.compile("open(sys.argv[sys.argv.index('-o')+1],'wb').write(b'partial');time.sleep(10)", timeout=0.2)
        self.assertFalse(ok);self.assertIn('timed out',log);self.assertLess(time.monotonic()-started,3)
        self.assertFalse(output.exists())

    def test_stdout_and_stderr_share_a_bounded_capture(self):
        for stream in (1,2):
            (ok, assembly, log), output=self.compile("while True:os.write(%d,b'x'*65536)" % stream, output_limit=1024)
            self.assertFalse(ok);self.assertIn('output limit',log);self.assertLessEqual(len(assembly),1024)
            self.assertLess(len(log),1200);self.assertFalse(output.exists())

    @unittest.skipUnless(sys.platform.startswith('linux'), 'uses Linux process state to verify actual descendant death')
    def test_descendants_with_inherited_or_closed_pipes_are_reaped_or_dead(self):
        for close_pipes in (False, True):
            pidfile=self.root/'descendant.pid'
            code=("child=os.fork()\nif child==0:\n" +
                  (" os.close(1);os.close(2)\n" if close_pipes else '') +
                  " time.sleep(5);os._exit(0)\n" +
                  "open(%r,'w').write(str(child))\n" % str(pidfile) +
                  "open(sys.argv[sys.argv.index('-o')+1],'wb').write(struct.pack('<5I',0x07230203,0x10000,0,1,0))\n" +
                  "print('; SPIR-V',flush=True)")
            (ok, _, log), _=self.compile(code, timeout=0.3)
            self.assertEqual(bool(ok),close_pipes,log)
            process=Path('/proc')/pidfile.read_text()/'stat'
            deadline=time.monotonic()+1
            while process.exists() and process.read_text().rsplit(')',1)[1].split()[0] != 'Z':
                self.assertLess(time.monotonic(),deadline,'compiler descendant survived owned-group cleanup')
                time.sleep(0.01)


class ActualCompiler(Fixture):
    def test_real_emission_compilation_validation_disassembly_and_companion(self):
        compiler=os.environ.get('ESTERIA_TEST_GLSLANG') or shutil.which('glslangValidator') or shutil.which('glslang')
        if not compiler:self.skipTest('actual glslang unavailable; supervisor fixtures do not qualify compilation')
        self.put('vertex', WEATHER);self.put('pixel', PIXEL)
        self.assertEqual(self.run_translation(compiler=compiler),0,self.translation_log)
        manifest=(self.output/'manifest.txt').read_text()
        self.assertIn('companion ',manifest)
        modules=list(self.output.glob('*/*.spv'));self.assertEqual(len(modules),3)
        for path in modules:
            self.assertEqual(path.read_bytes()[:4],b'\x03\x02\x23\x07')
            self.assertIn('; SPIR-V',path.with_suffix('.spvasm').read_text())
        invalid=self.root/'invalid.frag';invalid.write_text('#version 450\nvoid main(){missing_symbol();}\n')
        ok, _, _=translate.compile_glsl(compiler,str(invalid),str(self.root/'invalid.spv'),'frag')
        self.assertFalse(ok)


if __name__ == '__main__':unittest.main()
