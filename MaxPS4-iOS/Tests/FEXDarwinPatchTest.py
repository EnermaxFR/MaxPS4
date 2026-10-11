"""Regression tests using a real, pristine pinned FEXCore checkout."""
import importlib.util
from pathlib import Path
import shutil
import sys
import tempfile
import unittest

checkout = Path(sys.argv.pop(1))
script = Path(__file__).resolve().parents[2] / 'scripts/patch-fexcore-ios.py'
spec = importlib.util.spec_from_file_location('patch_fex', script)
patch = importlib.util.module_from_spec(spec)
spec.loader.exec_module(patch)
files = (
    'FEXCore/include/FEXCore/Utils/WritePriorityMutex.h',
    'FEXCore/include/FEXCore/Utils/SignalScopeGuards.h',
    'FEXCore/include/FEXCore/Utils/PrctlUtils.h',
    'FEXCore/Source/Interface/Core/CPUBackend.cpp',
    'FEXCore/Source/Interface/Core/JIT/MiscOps.cpp',
)


class DarwinPatchTest(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        for relative in files:
            dest = self.root / relative
            dest.parent.mkdir(parents=True, exist_ok=True)
            shutil.copyfile(checkout / relative, dest)

    def contents(self):
        return {relative: (self.root / relative).read_text() for relative in files}

    def test_idempotent_and_preserves_other_platforms(self):
        originals = self.contents()
        patch.adapt(self.root)
        patched = self.contents()
        self.assertTrue(all(patch.TAG in value for value in patched.values()))
        signals = patched[files[1]]
        # The complete original Linux RAII class is retained behind #else.
        original_class = originals[files[1]].split('class ScopedSignalMasker final {', 1)[1].split('\n/**', 1)[0]
        self.assertIn('class ScopedSignalMasker final {' + original_class, signals)
        self.assertIn('Number < NSIG && Number <= 64', signals)
        processor = patched[files[4]].split('DEF_OP(ProcessorID) {', 1)[1]
        darwin, others = processor.split('#else', 1)
        self.assertNotIn('TPIDRRO', darwin)
        self.assertNotIn('SYS_getcpu', darwin)
        self.assertIn('SYS_getcpu', others)
        self.assertIn('#if !defined(_WIN32) && !defined(__APPLE__)\n// MDWE', patched[files[3]])
        patch.adapt(self.root)
        self.assertEqual(self.contents(), patched)

    def test_source_drift_fails_without_partial_writes(self):
        target = self.root / files[3]
        target.write_text(target.read_text().replace('// MDWE', '// CHANGED', 1))
        before = self.contents()
        with self.assertRaises(ValueError):
            patch.adapt(self.root)
        self.assertEqual(before, self.contents())


unittest.main()
