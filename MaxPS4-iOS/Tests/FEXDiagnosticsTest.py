"""Check symbol accounting and avoid contract matches inside comments."""
import importlib.util
from pathlib import Path
import unittest

root = Path(__file__).resolve().parents[2]

def module(name):
    spec = importlib.util.spec_from_file_location(name, root / 'scripts' / (name + '.py'))
    instance = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(instance)
    return instance

links = module('fexcore-link-report')
contracts = module('audit-fex-darwin-contracts')

class DiagnosticsTest(unittest.TestCase):
    def test_cross_object_definitions_are_not_missing(self):
        # Real Mach-O-style names: one leading ABI underscore, plus mangling.
        before = '''a.o:
                 U __ZN6LogMan3Msg8MFmtImplEv
                 U _memcpy
                 U _memcpy
0000000000000000 T _entry
'''
        after = before + '''b.o:
0000000000000040 T __ZN6LogMan3Msg8MFmtImplEv
                 U _sys_icache_invalidate
'''
        result = links.report(before, after, 'c++filt')
        self.assertEqual(result['before_count'], 2)
        self.assertEqual(result['remaining_count'], 2)
        self.assertEqual(result['resolved'], ['__ZN6LogMan3Msg8MFmtImplEv'])
        self.assertEqual(result['new_dependencies'], ['_sys_icache_invalidate'])
        self.assertFalse(result['complete_link_proven'])
        self.assertTrue(all(row['category'].startswith('SDK/runtime') for row in result['remaining']))

    def test_runtime_imports_and_cache_portability_are_distinct(self):
        result = links.report('U ___clear_cache\nU _memcpy\n', 'U ___clear_cache\nU _memcpy\n', 'c++filt')
        self.assertEqual(result['categories']['Darwin cache portability'], 1)
        self.assertEqual(result['categories']['SDK/runtime import; validate with linker'], 1)

    def test_comments_do_not_satisfy_darwin_contracts(self):
        pattern = contracts.CONTRACTS['GetWritableAddress'][1]
        self.assertIsNone(contracts.declaration('// void* GetWritableAddress(void*);\n/* void* GetWritableAddress(void*); */', pattern))
        text = '// example only\n/* multiline\ncomment */\nvoid* GetWritableAddress(void*);'
        self.assertEqual(contracts.declaration(text, pattern), 4)

unittest.main()
