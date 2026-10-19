#!/usr/bin/env python3
import importlib.util
import json
import pathlib
import unittest

spec = importlib.util.spec_from_file_location('report_orbis', pathlib.Path(__file__).with_name('report-orbis-imports.py'))
module = importlib.util.module_from_spec(spec)
spec.loader.exec_module(module)


class ReportTest(unittest.TestCase):
    def fixture(self):
        row = dict(file='probe', nid='AAAAAAAAAAA', library='libSceNpTrophy', module='libSceNpTrophy',
                   library_version='1', module_version='257', type='2', binding='1', name='sceNpTrophyUnlockTrophy',
                   metadata_status='missing-export', behavior='no-export', game_execution='not-observed')
        log = ('provider="libSceNpTrophy.prx" status=accessible coverage=partial\n'
               'import consumer="probe" nid="AAAAAAAAAAA" library="libSceNpTrophy" module="libSceNpTrophy" '
               'library_version=1 module_version=257 type=2 status=metadata-match provider="libSceNpTrophy.prx" '
               'behavior=explicit-refusal execution-observed=false\n'
               '[ORBIS.GLOBAL] missing=0 unresolved=0 metadata_matches=1 admission=false\n')
        return row, log

    def test_refusal_is_not_functionality(self):
        row, log = self.fixture()
        output, report = module.analyse([row], log)
        self.assertEqual(output[0]['category'], 'network-psn')
        self.assertEqual(report['unresolved_before'], 1)
        self.assertEqual(report['categories']['network-psn']['explicit-refusal'], 1)
        self.assertFalse(report['admission'])
        self.assertEqual(row['metadata_status'], 'missing-export')

    def test_identity_mismatch_and_corrupt_counts(self):
        row, log = self.fixture()
        for changed in [log.replace('module_version=257', 'module_version=258'),
                        log.replace('unresolved=0', 'unresolved=1'),
                        log.replace('execution-observed=false', 'execution-observed=true'), log + log]:
            with self.assertRaises((ValueError, KeyError)):
                module.analyse([row], changed)

    def test_categories(self):
        cases = [('', '__tls_get_addr', 'memory-threads'), ('', '__cxa_finalize', 'loading-initialization'),
                 ('libSceAudioOut', 'sceAudioOutInit', 'audio'), ('libScePngDec', 'scePngDecDecode', 'graphics'),
                 ('libSceGnmDriver', 'sceGnmDrawInitDefaultHardwareState350', 'graphics'),
                 ('libSceSaveData', 'sceSaveDataInitialize3', 'files-storage'),
                 ('libkernel', 'sceKernelOpen', 'files-storage'), ('libkernel', 'gettimeofday', 'system-other')]
        for lib, name, expected in cases:
            self.assertEqual(module.category(dict(library=lib, name=name)), expected)

    def test_json_escaping(self):
        value = 'space " slash \\ control\n'
        self.assertEqual(module.fields('name=' + json.dumps(value))['name'], value)
        with self.assertRaises(ValueError):
            module.fields('name="x" garbage')


if __name__ == '__main__':
    unittest.main()
