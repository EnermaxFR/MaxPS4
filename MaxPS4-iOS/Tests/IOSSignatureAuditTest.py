"""Signature audit must distinguish signed grants, profile grants and unknown."""
import importlib.util
from pathlib import Path
import plistlib
import subprocess
import tempfile
import unittest
from unittest.mock import patch

root = Path(__file__).resolve().parents[2]
spec = importlib.util.spec_from_file_location('signature', root / 'scripts/audit-ios-signature.py')
audit = importlib.util.module_from_spec(spec)
spec.loader.exec_module(audit)


class SignatureAuditTest(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.app = Path(self.temp.name)
        (self.app / 'Info.plist').write_bytes(plistlib.dumps({
            'CFBundleExecutable': 'MaxPS4', 'CFBundleIdentifier': 'com.enermaxfr.maxps4'}))

    def result(self, code, data=b''):
        return subprocess.CompletedProcess([], code, stdout=data, stderr=b'')

    def test_unsigned_is_unknown(self):
        with patch.object(audit.subprocess, 'run', side_effect=[self.result(1), self.result(1)]):
            report = audit.inspect(self.app)
        self.assertIsNone(report['signed_entitlements'])
        self.assertIsNone(report['profile_entitlements'])
        self.assertFalse(report['jit_execution_proven'])

    def test_profile_does_not_override_signed_entitlements(self):
        (self.app / 'embedded.mobileprovision').write_bytes(b'fixture')
        signed = plistlib.dumps({'get-task-allow': False})
        profile = plistlib.dumps({'Entitlements': {'get-task-allow': True}})
        with patch.object(audit.subprocess, 'run', side_effect=[
                self.result(0), self.result(0, signed), self.result(0, profile)]):
            report = audit.inspect(self.app)
        self.assertFalse(report['signed_entitlements']['get-task-allow'])
        self.assertTrue(report['profile_entitlements']['get-task-allow'])
        self.assertFalse(report['jit_execution_proven'])
        self.assertEqual(report['bundle_identifier'], 'com.enermaxfr.maxps4')

    def test_empty_entitlement_output_is_unknown(self):
        with patch.object(audit.subprocess, 'run', side_effect=[self.result(0), self.result(0)]):
            self.assertIsNone(audit.inspect(self.app)['signed_entitlements'])


unittest.main()
