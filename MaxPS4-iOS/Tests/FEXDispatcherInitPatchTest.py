"""Use pristine pinned Darwin sources, never a fabricated dispatcher fixture."""
import importlib.util
from pathlib import Path
import shutil
import sys
import tempfile
import unittest

source = Path(sys.argv.pop(1))
script = Path(__file__).resolve().parents[2] / 'scripts/patch-fexcore-dispatcher-init.py'
spec = importlib.util.spec_from_file_location('dispatcher_patch', script)
patch = importlib.util.module_from_spec(spec)
spec.loader.exec_module(patch)


class DispatcherPatchTest(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        for name in patch.PATCHES:
            dest = self.root / name
            dest.parent.mkdir(parents=True, exist_ok=True)
            shutil.copyfile(source / name, dest)

    def contents(self):
        return {name: (self.root / name).read_text() for name in patch.PATCHES}

    def test_idempotent(self):
        patch.adapt(self.root)
        before = self.contents()
        patch.adapt(self.root)
        self.assertEqual(before, self.contents())

    def test_divergence_has_no_partial_writes(self):
        path = self.root / 'FEXCore/Source/Interface/Core/Core.cpp'
        path.write_text(path.read_text().replace('Dispatcher::Create(this)', 'Dispatcher::Changed(this)'))
        before = self.contents()
        with self.assertRaises(ValueError):
            patch.adapt(self.root)
        self.assertEqual(before, self.contents())

    def test_modified_applied_patch_rejected(self):
        patch.adapt(self.root)
        path = self.root / 'FEXCore/Source/Interface/Core/Core.cpp'
        path.write_text(path.read_text().replace('if (!Dispatcher) return false;', '// removed check'))
        before = self.contents()
        with self.assertRaises(ValueError):
            patch.adapt(self.root)
        self.assertEqual(before, self.contents())


unittest.main()
