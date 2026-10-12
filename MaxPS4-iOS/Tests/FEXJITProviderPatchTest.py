"""Validate transactional patches against the real pinned FEX Darwin sources."""
import importlib.util
from pathlib import Path
import shutil
import sys
import tempfile
import unittest

source = Path(sys.argv.pop(1))
script = Path(__file__).resolve().parents[2] / 'scripts/patch-fexcore-jit-provider.py'
spec = importlib.util.spec_from_file_location('jit_patch', script)
patch = importlib.util.module_from_spec(spec)
spec.loader.exec_module(patch)

class ProviderPatchTest(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        for name in (patch.ALLOCATOR, patch.BACKEND):
            path = self.root / name
            path.parent.mkdir(parents=True, exist_ok=True)
            shutil.copyfile(source / name, path)

    def contents(self):
        return {name: (self.root / name).read_text() for name in (patch.ALLOCATOR, patch.BACKEND)}

    def test_idempotent(self):
        patch.adapt(self.root)
        before = self.contents()
        patch.adapt(self.root)
        self.assertEqual(before, self.contents())
        self.assertNotIn('dlopen(', before[patch.ALLOCATOR])
        self.assertNotIn('BreakGetJITMapping', before[patch.ALLOCATOR])
        self.assertIn('maxps4_jit_allocate(Size)', before[patch.ALLOCATOR])

    def test_divergence_is_transactional(self):
        path = self.root / patch.BACKEND
        path.write_text(path.read_text().replace('if (!Ptr || Ptr == reinterpret_cast<void*>(-1))', 'if (false)')
                        .replace('LOGMAN_THROW_A_FMT(!!Ptr,', 'LOGMAN_THROW_A_FMT(false,'))
        before = self.contents()
        with self.assertRaises(ValueError): patch.adapt(self.root)
        self.assertEqual(before, self.contents())

    def test_applied_context_verified(self):
        patch.adapt(self.root)
        path = self.root / patch.ALLOCATOR
        path.write_text(path.read_text().replace('return maxps4_jit_allocate(Size);', 'return nullptr;'))
        before = self.contents()
        with self.assertRaises(ValueError): patch.adapt(self.root)
        self.assertEqual(before, self.contents())

unittest.main()
