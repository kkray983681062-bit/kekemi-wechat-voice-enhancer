import struct
import tempfile
import unittest
from pathlib import Path

from wevoice.core import VoiceError, verify_files, locate, select_call
from wevoice.locator import Settings, valid_executable


def make_exe(path, machine=0x8664):
    data = bytearray(256)
    data[:2] = b'MZ'
    struct.pack_into('<I', data, 0x3c, 128)
    data[128:132] = b'PE\0\0'
    struct.pack_into('<H', data, 132, machine)
    path.write_bytes(data)


class SafetyTests(unittest.TestCase):
    def test_unknown_engine_is_rejected(self):
        with tempfile.TemporaryDirectory() as folder:
            p = Path(folder) / 'engine.dll'
            p.write_bytes(b'unrecognized build')
            with self.assertRaises(VoiceError):
                verify_files({n: {'path': str(p)} for n in
                              ('weixin.dll', 'voipengine.dll', 'confsdk.dll')})

    def test_missing_engine_is_rejected(self):
        with self.assertRaises(VoiceError):
            verify_files({})

    def test_disconnected_call_never_dereferences_null(self):
        class EmptyReader:
            def ptr(self, address):
                if address < 0x10000:
                    raise AssertionError('NULL dereference')
                return 0
        with self.assertRaises(VoiceError):
            locate(EmptyReader(), {'confsdk.dll': {'base': 0x100000},
                                   'voipengine.dll': {'base': 0x300000}})

    def test_ambiguous_calls_are_not_selected_arbitrarily(self):
        with self.assertRaises(VoiceError):
            select_call([{'pid': 101}, {'pid': 102}])

    def test_single_call_selected(self):
        self.assertEqual(select_call([{'pid': 101}]), {'pid': 101})

    def test_unicode_path_persists_and_stale_path_is_ignored(self):
        with tempfile.TemporaryDirectory(prefix='微信测试-') as folder:
            folder = Path(folder)
            p = folder / 'Weixin.exe'
            make_exe(p)
            settings = Settings(folder / 'settings.json')
            settings.save(str(p))
            self.assertEqual(Settings(settings.file).load(), str(p.resolve()))
            p.unlink()
            self.assertEqual(settings.load(), '')

    def test_malformed_settings_are_ignored(self):
        with tempfile.TemporaryDirectory() as folder:
            p = Path(folder) / 'settings.json'
            p.write_text('{bad json', encoding='utf-8')
            self.assertEqual(Settings(p).load(), '')

    def test_manual_selection_rejects_wrong_architecture_and_file(self):
        with tempfile.TemporaryDirectory() as folder:
            p = Path(folder) / 'Weixin.exe'
            make_exe(p, 0x14c)
            self.assertFalse(valid_executable(str(p)))
            make_exe(p)
            self.assertTrue(valid_executable(str(p)))
            wrong = p.with_name('not-wechat.exe')
            wrong.write_bytes(p.read_bytes())
            self.assertFalse(valid_executable(str(wrong)))


if __name__ == '__main__':
    unittest.main()
