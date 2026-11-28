"""Record full-screen build inputs and host-link workers; never a QNX build."""
import json
import os
from pathlib import Path
import shlex
import shutil
import subprocess
import sys
import tempfile
import unittest

NATIVE=Path(__file__).resolve().parents[1]
FULL=NATIVE.parent/'fullscreen/native'


class FullscreenBuildInputs(unittest.TestCase):
    def setUp(self):
        temporary=tempfile.TemporaryDirectory(prefix='full-build-inputs-')
        self.addCleanup(temporary.cleanup)
        self.root=Path(temporary.name)/'source tree'
        self.screen=self.root/'fullscreen';self.native=self.screen/'native'
        self.native.mkdir(parents=True)
        shutil.copy2(FULL/'build.sh',self.native/'build.sh')
        for name in ('src','sdk','telemetry','generated','usb'):
            (self.native/name).symlink_to(FULL/name,target_is_directory=True)
        shared=self.root/'native/src';shared.mkdir(parents=True)
        (shared/'common').symlink_to(NATIVE/'src/common',target_is_directory=True)
        inputs=self.root/'test inputs';inputs.mkdir()
        frameworks=inputs/'frameworks';frameworks.mkdir()
        for name in ('libAppFramework.A.so','libCoreFramework.A.so'):
            (frameworks/name).write_text('Empty host-test input; not a framework.\n')
        runtime=inputs/'runtime';runtime.mkdir()
        (runtime/'libscreen.so.1').write_text('Empty host-test input; not a QNX library.\n')
        host=inputs/'compiler recorder';binary=host/'usr/bin';binary.mkdir(parents=True)
        self.log=inputs/'commands.jsonl'
        recorder='#!'+sys.executable+'\n'+'''import json, os, pathlib, sys
with open(os.environ['FULL_BUILD_INPUTS_LOG'],'a') as log:
    log.write(json.dumps(sys.argv[1:])+'\\n')
if os.environ.get('FULL_BUILD_INPUTS_FAIL'): sys.exit(42)
output=pathlib.Path(sys.argv[sys.argv.index('-o')+1])
output.parent.mkdir(parents=True,exist_ok=True)
output.write_text('Host build-input fixture. Do not distribute or install.\\n')
'''
        (binary/'qcc').write_text(recorder);(binary/'qcc').chmod(0o700)
        self.env=dict(os.environ,QNX_HOST=str(host),QNX_TARGET=str(inputs/'unused target'),
            QNX_CONFIGURATION=str(inputs/'unused configuration'),NBTEVO_FRAMEWORKS_DIR=str(frameworks),
            NBTEVO_RUNTIME_DIR=str(runtime),QNX_OPENSSL_DIR=str(inputs/'unused openssl'),
            ID6_STANDARD_NATIVE_DIR='',FULL_BUILD_INPUTS_LOG=str(self.log),FULL_BUILD_INPUTS_FAIL='')

    def run_driver(self):
        result=subprocess.run(['sh',str(self.native/'build.sh')],env=self.env,capture_output=True,text=True,timeout=30)
        commands=[json.loads(line) for line in self.log.read_text().splitlines()]
        return result,commands

    def test_unique_sources_existing_inputs_and_object_order(self):
        result,commands=self.run_driver();self.assertEqual(result.returncode,0,result.stderr)
        self.assertGreater(len(commands),12)
        outputs=set()
        for args in commands:
            self.assertIn('-mno-unaligned-access',args)
            self.assertIn('-Wall',args);self.assertIn('-Wextra',args);self.assertIn('-Werror',args)
            if '-lang-c++' in args:
                self.assertIn('-V4.4.2,gcc_ntoarmv7le_gpp',args)
                self.assertIn('-Wc,-std=gnu++98',args)
            else:
                self.assertIn('-V4.4.2,gcc_ntoarmv7le',args)
                self.assertIn('-Wc,-std=c99',args)
            output=args[args.index('-o')+1]
            inputs=[item for item in args if item.endswith(('.c','.cpp','.o')) and item!=output]
            self.assertEqual(len(inputs),len(set(inputs)),output)
            for item in inputs:
                if item.endswith('.o'): self.assertIn(item,outputs)
                else: self.assertTrue((self.screen/item).is_file(),item)
            self.assertNotIn(output,outputs);outputs.add(output)

    def test_actual_emitted_boost_and_warm_inputs_link_with_host_fakes(self):
        result,commands=self.run_driver();self.assertEqual(result.returncode,0,result.stderr)
        objects={args[args.index('-o')+1]:args[args.index('-c')+1] for args in commands if '-c' in args}
        for worker in ('boost_deck_worker.c','warm_deck_worker.c'):
            matches=[args for args in commands if 'native/telemetry/workers/'+worker in args]
            self.assertEqual(len(matches),1)
            args=matches[0];output_index=args.index('-o');host_args=[]
            if worker=='boost_deck_worker.c':
                self.assertIn('native/telemetry/workers/boost_tone.c',args)
            for index,arg in enumerate(args):
                if index in (output_index,output_index+1): continue
                if arg.startswith(('-V','-Wc,')) or arg in ('-mno-unaligned-access','-lsocket','-Lnative/out/link'): continue
                # QNX PCM remains in the recorded target inputs. The host
                # worker has no __QNX__ alert entry and cannot compile its API.
                if arg=='native/telemetry/workers/boost_tone.c': continue
                host_args.append(objects.get(arg,arg))
            host_args+=['-std=c99','-DMOST_HOST','-pthread','-I'+str(NATIVE/'tests'),
                *[str(NATIVE/'tests'/name) for name in ('fake_most_env.c','fake_dsi.c','fake_dme.c')],
                '-lm','-o',str(self.root/worker)]
            result=subprocess.run(shlex.split(os.environ.get('CC','cc'))+host_args,
                cwd=self.screen,capture_output=True,text=True,timeout=60)
            self.assertEqual(result.returncode,0,result.stderr)

    def test_failed_compiler_stops_before_later_commands(self):
        self.env['FULL_BUILD_INPUTS_FAIL']='1'
        result,commands=self.run_driver()
        self.assertEqual(result.returncode,42);self.assertEqual(len(commands),1)


if __name__=='__main__': unittest.main()
