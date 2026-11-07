"""Portable, bounded discovery; only remembers a user-selected executable path."""
import ctypes
import json
import os
import struct
import winreg
from pathlib import Path
import psutil


def valid_executable(path):
    try:
        p = Path(path)
        if p.name.lower() not in ('weixin.exe', 'wechat.exe'):
            return False
        with p.open('rb') as stream:
            header = stream.read(64)
            if header[:2] != b'MZ' or len(header) != 64:
                return False
            offset = struct.unpack_from('<I', header, 0x3c)[0]
            if not 64 <= offset <= 1048576:
                return False
            stream.seek(offset)
            pe = stream.read(6)
        return pe[:4] == b'PE\0\0' and pe[4:] == b'\x64\x86'
    except (OSError, TypeError, ValueError):
        return False


def settings_path():
    # Known-folder lookup works with redirected Windows profiles too.
    buf = ctypes.create_unicode_buffer(32768)
    result = ctypes.windll.shell32.SHGetFolderPathW(None, 0x1c, None, 0, buf)
    base = Path(buf.value) if result == 0 and buf.value else Path(os.environ['LOCALAPPDATA'])
    return base / 'KekemiWeChatVoiceEnhancer' / 'settings.json'


class Settings:
    def __init__(self, path=None):
        self.file = Path(path) if path else settings_path()

    def save(self, path):
        if not valid_executable(path):
            raise ValueError('请选择 64 位微信的 Weixin.exe 或 WeChat.exe。')
        self.file.parent.mkdir(parents=True, exist_ok=True)
        tmp = self.file.with_suffix('.tmp')
        tmp.write_text(json.dumps({'wechat_path': str(Path(path).resolve())},
                                  ensure_ascii=False, indent=2), encoding='utf-8')
        tmp.replace(self.file)

    def load(self):
        try:
            value = json.loads(self.file.read_text(encoding='utf-8')).get('wechat_path', '')
            return str(Path(value).resolve()) if isinstance(value, str) and valid_executable(value) else ''
        except (OSError, ValueError, AttributeError):
            return ''


def running_processes():
    result = []
    for proc in psutil.process_iter(['pid', 'name', 'exe']):
        try:
            if (proc.info['name'] or '').lower() in ('weixin.exe', 'wechat.exe'):
                result.append(proc.info)
        except (psutil.Error, OSError):
            continue
    return result


def registry_candidates():
    for hive in (winreg.HKEY_CURRENT_USER, winreg.HKEY_LOCAL_MACHINE):
        for view in (winreg.KEY_WOW64_64KEY, winreg.KEY_WOW64_32KEY):
            for app in ('Weixin.exe', 'WeChat.exe'):
                try:
                    with winreg.OpenKey(hive, 'SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\App Paths\\' + app,
                                        0, winreg.KEY_READ | view) as key:
                        yield winreg.QueryValueEx(key, '')[0].strip('"')
                except OSError:
                    pass
            for app in ('Weixin', 'WeChat'):
                try:
                    with winreg.OpenKey(hive, 'SOFTWARE\\Tencent\\' + app,
                                        0, winreg.KEY_READ | view) as key:
                        folder = winreg.QueryValueEx(key, 'InstallPath')[0]
                        yield str(Path(folder) / (app + '.exe'))
                except OSError:
                    pass
            try:
                with winreg.OpenKey(hive, r'SOFTWARE\Microsoft\Windows\CurrentVersion\Uninstall',
                                    0, winreg.KEY_READ | view) as root:
                    for i in range(winreg.QueryInfoKey(root)[0]):
                        try:
                            with winreg.OpenKey(root, winreg.EnumKey(root, i)) as key:
                                display = winreg.QueryValueEx(key, 'DisplayName')[0]
                                if not any(word in display.lower() for word in ('微信', 'weixin', 'wechat')):
                                    continue
                                for prop in ('InstallLocation', 'DisplayIcon'):
                                    try:
                                        raw = winreg.QueryValueEx(key, prop)[0]
                                        if prop == 'DisplayIcon':
                                            yield raw.rsplit(',', 1)[0].strip().strip('"')
                                        else:
                                            for name in ('Weixin.exe', 'WeChat.exe'):
                                                yield str(Path(raw) / name)
                                    except OSError:
                                        pass
                        except OSError:
                            continue
            except OSError:
                pass


def discover(settings):
    saved = settings.load()
    if saved:
        return saved, '已记住的位置'
    candidates = [p['exe'] for p in running_processes() if p.get('exe')]
    candidates.extend(registry_candidates())
    for variable in ('ProgramFiles', 'ProgramFiles(x86)', 'LOCALAPPDATA'):
        base = os.environ.get(variable)
        if base:
            for name in ('Weixin', 'WeChat'):
                candidates.append(str(Path(base) / 'Tencent' / name / (name + '.exe')))
    # Fixed, shallow locations on local disks; never recursively scan user files.
    mask = ctypes.windll.kernel32.GetLogicalDrives()
    for i in range(26):
        drive = f'{chr(65+i)}:\\'
        if mask & (1 << i) and ctypes.windll.kernel32.GetDriveTypeW(drive) == 3:
            for prefix in ('Program Files/Tencent', 'Program Files (x86)/Tencent', 'Tencent', ''):
                for name in ('Weixin', 'WeChat', '微信'):
                    for exe in ('Weixin.exe', 'WeChat.exe'):
                        candidates.append(str(Path(drive) / prefix / name / exe))
    for path in dict.fromkeys(candidates):
        if valid_executable(path):
            try:
                settings.save(path)
                return str(Path(path).resolve()), '自动识别 · 已记住'
            except OSError:
                return str(Path(path).resolve()), '已识别 · 保存失败'
    return '', '未找到微信'
