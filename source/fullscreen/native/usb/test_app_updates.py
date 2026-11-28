"""Exercise the signed USB install and rollback logic on a temporary filesystem."""
from hashlib import sha256
import os
from pathlib import Path
import shlex
import struct
import subprocess
import tempfile
import unittest


class UsbInstallTest(unittest.TestCase):
    def setUp(self):
        temporary = tempfile.TemporaryDirectory()
        self.addCleanup(temporary.cleanup)
        root = Path(temporary.name)
        disk = root / 'disk'
        self.plugins = disk / 'Plugins'
        updater = self.plugins / 'ID6LibraryLab'
        updater.mkdir(parents=True)
        self.lock = disk / 'GaugeDeck-install-lock'
        (updater / 'panel.txt').write_text('1280x480\n')
        frameworks = root / 'frameworks'
        frameworks.mkdir()
        self.framework = frameworks / 'libAppFramework.A.so'
        self.framework.write_bytes(b'app')
        (frameworks / 'libCoreFramework.A.so').write_bytes(b'core')
        usb = root / 'fs/usb0'
        usb.mkdir(parents=True)
        self.bundle = usb / 'ID6Apps.update'
        private = root / 'test.pem'
        self.openssl('genpkey', '-algorithm', 'RSA', '-pkeyopt', 'rsa_keygen_bits:3072', '-out', private)
        self.openssl('pkey', '-in', private, '-pubout', '-outform', 'DER', '-out', updater / 'update-key.der')
        data = root / 'unsigned'
        body = b'ID6UPD1\n' + struct.pack('>II', 115, 1) + sha256(b'app').digest() + sha256(b'core').digest()
        names = {0:'BoostDeck',1:'ShiftDeck',4:'Bootmod3Suite',7:'XDriveControl',12:'WarmDeck'}
        for app_id, name in names.items():
            body += struct.pack('>II', app_id, 4)
            for _ in range(4 if app_id == 7 else 5):
                payload = name.encode()
                body += struct.pack('>I', len(payload)) + payload
        data.write_bytes(body)
        sig = self.openssl('dgst','-sha256','-sign',private,data).stdout
        self.bundle.write_bytes(body + sig)
        failure = root / 'failure.c'
        failure.write_text('''
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
int test_rename(const char *from, const char *to) {
    if (getenv("ID6_TEST_RENAME") && strstr(from, "/new-WarmDeck") && strstr(to, "/Plugins/WarmDeck")) {
        errno = EIO;
        return -1;
    }
    return rename(from,to);
}
''')
        flags = shlex.split(subprocess.check_output(['pkg-config','--cflags','--libs','openssl'],text=True))
        defines = dict(ID6_ROOT=disk, ID6_FS=root/'fs', ID6_MNT=root/'mnt', ID6_FRAMEWORKS=frameworks)
        self.worker = root / 'worker'
        source = Path(__file__).with_name('app_updates.c')
        subprocess.run(['cc','-std=c99','-Wall','-Wextra','-Werror','-Drename=test_rename',
                        *(f'-D{k}="{v}"' for k,v in defines.items()),'-c',source,'-o',root/'worker.o',
                        *[flag for flag in flags if flag.startswith('-I')]],check=True)
        subprocess.run(['cc',root/'worker.o',failure,'-o',self.worker,*flags],check=True)

    @staticmethod
    def openssl(*args):
        return subprocess.run(['openssl',*args],check=True,capture_output=True)

    def run_worker(self, mode, fail=False):
        env = os.environ.copy()
        if fail:
            env['ID6_TEST_RENAME'] = '1'
        return subprocess.run([self.worker,mode],capture_output=True,text=True,env=env)

    def install_original(self, app):
        live = self.plugins / app
        live.mkdir()
        (live / f'{app}.manifest').write_text('original')
        (live / 'gateway-ip.txt').write_text('192.0.2.8\n')
        return live

    def test_install_and_update_are_distinct(self):
        boost = self.install_original('BoostDeck')
        result = self.run_worker('--install-new')
        self.assertEqual(result.returncode,0,result.stdout)
        self.assertIn('Installed 2 apps',result.stdout)
        self.assertEqual((boost/'BoostDeck.manifest').read_text(),'original')
        self.assertTrue((self.plugins/'ShiftDeck/.id6-usb-version').exists())
        self.assertFalse((self.plugins/'Bootmod3Suite').exists())
        self.assertFalse((self.plugins/'XDriveControl').exists())
        result = self.run_worker('--update')
        self.assertEqual(result.returncode,0,result.stdout)
        self.assertIn('Updated 1 app',result.stdout)
        self.assertEqual((boost/'BoostDeck.manifest').read_text(),'BoostDeck')
        self.assertEqual((boost/'gateway-ip.txt').read_text(),'192.0.2.8\n')
        self.assertIn('No matching',self.run_worker('--update').stdout)

    def test_missing_apps_are_rolled_back_on_publish_failure(self):
        boost = self.install_original('BoostDeck')
        result = self.run_worker('--install-new',fail=True)
        self.assertNotEqual(result.returncode,0)
        self.assertIn('restored',result.stdout)
        self.assertFalse((self.plugins/'ShiftDeck').exists())
        self.assertFalse((self.plugins/'WarmDeck').exists())
        self.assertFalse(self.lock.exists())
        self.assertEqual((boost/'BoostDeck.manifest').read_text(),'original')

    def test_installed_apps_are_rolled_back_on_publish_failure(self):
        boost = self.install_original('BoostDeck')
        warm = self.install_original('WarmDeck')
        result = self.run_worker('--update',fail=True)
        self.assertIn('restored',result.stdout)
        self.assertEqual((boost/'BoostDeck.manifest').read_text(),'original')
        self.assertEqual((warm/'WarmDeck.manifest').read_text(),'original')

    def test_signature_framework_and_panel_are_required(self):
        panel = self.plugins/'ID6LibraryLab/panel.txt'
        panel.write_text('960x480\n')
        self.assertIn('No supported',self.run_worker('--install-new').stdout)
        self.framework.write_bytes(b'different firmware')
        self.assertIn('firmware',self.run_worker('--install-new').stdout)
        data = self.bundle.read_bytes()
        self.bundle.write_bytes(data[:-1] + bytes([data[-1] ^ 1]))
        self.assertIn('signature failed',self.run_worker('--install-new').stdout)
        self.assertFalse((self.plugins/'BoostDeck').exists())


if __name__ == '__main__':
    unittest.main()
