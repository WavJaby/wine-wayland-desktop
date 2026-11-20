"""Real signature and recovery tests on disposable Wine PE copies."""
import json
import os
from pathlib import Path
import signal
import subprocess
import tempfile
import time
import unittest

ROOT = Path(__file__).resolve().parents[1]
SOURCE = Path(os.environ.get('WINE_TEST_PE', '/nonexistent'))
JSIGN = Path(os.environ.get('WINE_TEST_JSIGN', '/nonexistent'))
JAVA = os.environ.get('WINE_TEST_JAVA', 'java')
SIGNER = Path(os.environ.get('WINE_TEST_SIGNER', '/nonexistent'))
RELATIVE = 'drive_c/windows/system32/crypt32.dll'
ACK = ('--acknowledge', 'private-testing-v1')


class Prefix:
    """A disposable prefix holding a private copy of the Wine PE under test; the pristine runner copy is `source`."""

    def __init__(self, tmp, test):
        self.test = test
        self.prefix = Path(tmp)
        self.source = self.prefix / 'runner.dll'
        self.source.write_bytes(SOURCE.read_bytes())
        self.before = self.source.read_bytes()
        self.target = self.prefix / RELATIVE
        self.target.parent.mkdir(parents=True)

    def state(self):
        return next((self.prefix / '.wine-wayland-signing').glob('*'), None)

    def run(self, op, expected=0, extra=(), java=JAVA, verifier=SIGNER, jsign=JSIGN, **kwargs):
        result = subprocess.run(['bash', str(ROOT / 'scripts/local-sign.sh'), op,
            '--prefix', str(self.prefix), '--file', RELATIVE, '--source', str(self.source),
            '--verifier', str(verifier), '--jsign-jar', str(jsign), '--java', str(java), *extra],
            capture_output=True, **kwargs)
        self.test.assertEqual(result.returncode, expected, result.stderr.decode())
        return result


def prefixes(test):
    tmp = tempfile.TemporaryDirectory(prefix='sign test ')
    test.addCleanup(tmp.cleanup)
    prefix = Prefix(tmp.name, test)
    prefix.target.write_bytes(prefix.before)
    return prefix


@unittest.skipUnless(SOURCE.is_file() and SIGNER.is_file() and JSIGN.is_file(), 'Set WINE_TEST_PE, WINE_TEST_SIGNER and WINE_TEST_JSIGN')
class TransactionTests(unittest.TestCase):
    def test_private_copy_and_restore(self):
        for identity, kind in ((identity, kind) for identity in ('neutral', 'custom')
                               for kind in ('copy', 'hardlink', 'symlink')):
            with self.subTest(identity=identity, kind=kind):
                p = prefixes(self)
                p.target.unlink()
                if kind == 'hardlink':
                    os.link(p.source, p.target)
                elif kind == 'symlink':
                    p.target.symlink_to(p.source)
                else:
                    p.target.write_bytes(p.before)
                config = p.prefix / 'my certificate.cnf'
                config.write_text('[req]\nprompt=no\ndistinguished_name=dn\n'
                                  '[dn]\nCN=Independent Local Test\nO=Personal Testing\n')
                options = ('--certificate-config', str(config)) if identity == 'custom' else ()
                p.run('inspect', extra=options)
                p.run('apply', 1, options)  # Notice cannot be silently accepted.
                p.run('apply', extra=(*options, *ACK))
                manifest_path = p.state() / 'manifest.json'
                manifest = json.loads(manifest_path.read_text())
                self.assertEqual(manifest['certificate_profile'], identity)
                self.assertEqual(manifest['trust_changes'], [])
                if identity == 'custom':
                    self.assertIn('CN=Independent Local Test', manifest['certificate_subject'])
                    self.assertEqual((manifest_path.parent / 'certificate.cnf').read_text(), config.read_text())
                signed = p.target.read_bytes()
                self.assertNotEqual(signed, p.before)
                self.assertEqual(p.source.read_bytes(), p.before)
                p.run('apply', 1, (*options, *ACK))
                p.target.write_bytes(b'updated')
                p.run('restore', 1, options)
                self.assertEqual(p.target.read_bytes(), b'updated')
                p.target.write_bytes(signed)
                p.run('restore', extra=options)
                self.assertEqual(p.target.read_bytes(), p.before)
                self.assertEqual(p.target.is_symlink(), kind == 'symlink')
                p.run('restore', extra=options)

    def assert_clean_slate_then_apply(self, p):
        """A failed or interrupted apply left the target untouched and no transaction files, and the slot is reusable."""
        self.assertEqual(p.target.read_bytes(), p.before)
        state = p.state()
        leftovers = [] if state is None else sorted(x.name for x in state.iterdir() if x.name != 'lock')
        self.assertEqual(leftovers, [])
        p.run('apply', extra=ACK)
        self.assertNotEqual(p.target.read_bytes(), p.before)
        p.run('restore')
        self.assertEqual(p.target.read_bytes(), p.before)

    def test_failed_verification_rolls_back(self):
        p = prefixes(self)
        p.run('apply', 1, ACK, verifier='/bin/false')
        self.assert_clean_slate_then_apply(p)

    def test_missing_jsign_fails_before_any_state(self):
        p = prefixes(self)
        p.run('apply', 1, ACK, jsign=p.prefix / 'missing.jar')
        self.assert_clean_slate_then_apply(p)

    def interrupt_apply(self, p, sig):
        """Start an apply whose signing step blocks, deliver `sig` to its whole process group, wait for it to die."""
        started = p.prefix / 'started'
        slow = p.prefix / 'slow java.sh'
        slow.write_text(f'#!/bin/sh\ntouch "{started}"\nexec sleep 60\n')
        slow.chmod(0o755)
        process = subprocess.Popen(['bash', str(ROOT / 'scripts/local-sign.sh'), 'apply',
            '--prefix', str(p.prefix), '--file', RELATIVE, '--source', str(p.source),
            '--verifier', str(SIGNER), '--jsign-jar', str(JSIGN), '--java', str(slow), *ACK],
            stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, start_new_session=True)
        deadline = time.monotonic() + 30
        while not started.exists() and time.monotonic() < deadline:
            time.sleep(0.05)
        self.assertTrue(started.exists(), 'signing never started')
        os.killpg(process.pid, sig)
        process.wait(timeout=30)

    def test_interrupted_apply_rolls_back(self):
        p = prefixes(self)
        self.interrupt_apply(p, signal.SIGTERM)
        self.assert_clean_slate_then_apply(p)

    def test_killed_apply_is_recoverable(self):
        """SIGKILL runs no handler: staging stays, but without a manifest the target is untouched and the next apply drops it."""
        p = prefixes(self)
        self.interrupt_apply(p, signal.SIGKILL)
        self.assertTrue((p.state() / 'key.pem').exists())
        self.assertFalse((p.state() / 'manifest.json').exists())
        self.assertEqual(p.target.read_bytes(), p.before)
        p.run('restore', 1)
        p.run('apply', extra=ACK)
        self.assertNotEqual(p.target.read_bytes(), p.before)
        p.run('restore')
        self.assertEqual(p.target.read_bytes(), p.before)

    def test_zombie_process_does_not_block(self):
        """A defunct process of the same user has no environment to read and cannot be a Wine client."""
        p = prefixes(self)
        zombie = subprocess.Popen(['true'])
        self.addCleanup(zombie.wait)
        for _ in range(100):
            state = Path(f'/proc/{zombie.pid}/status').read_text()
            if '\nState:\tZ' in state:
                break
            time.sleep(0.05)
        self.assertIn('\nState:\tZ', state)
        p.run('apply', extra=ACK)
        p.run('restore')
        self.assertEqual(p.target.read_bytes(), p.before)


if __name__ == '__main__':
    unittest.main()
