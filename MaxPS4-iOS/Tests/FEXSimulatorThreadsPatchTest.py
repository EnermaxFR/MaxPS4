"""Use pristine pinned Darwin sources, never a fabricated simulator fixture."""
import importlib.util
from pathlib import Path
import shutil
import sys
import tempfile
import unittest

source = Path(sys.argv.pop(1))
script = Path(__file__).resolve().parents[2] / 'scripts/patch-fexcore-simulator-threads.py'
spec = importlib.util.spec_from_file_location('simulator_patch', script)
patch = importlib.util.module_from_spec(spec)
spec.loader.exec_module(patch)


class SimulatorPatchTest(unittest.TestCase):
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
        path = self.root / 'External/vixl/src/aarch64/simulator-aarch64.cc'
        path.write_text(path.read_text().replace('void Simulator::VisitAtomicMemory(', 'void Simulator::Changed('))
        before = self.contents()
        with self.assertRaises(ValueError):
            patch.adapt(self.root)
        self.assertEqual(before, self.contents())

    def test_modified_applied_patch_rejected(self):
        patch.adapt(self.root)
        path = self.root / 'FEXCore/Source/Interface/Core/Dispatcher/Dispatcher.cpp'
        path.write_text(path.read_text().replace('thread_local PerThread', 'static PerThread'))
        before = self.contents()
        with self.assertRaises(ValueError):
            patch.adapt(self.root)
        self.assertEqual(before, self.contents())


unittest.main()
