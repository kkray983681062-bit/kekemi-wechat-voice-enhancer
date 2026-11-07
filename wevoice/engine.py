"""Serialized controller lifecycle. Polls only read; native setter calls keep 128."""
import json
import os
from pathlib import Path
import frida
import psutil

from .core import VoiceError, locate, parameters, select_call, verify_files
from .locator import running_processes
from .winmem import Reader, modules


def path_key(value):
    return os.path.normcase(os.path.realpath(value))


class Probe:
    def __init__(self, pid):
        self.pid = pid
        self.created = psutil.Process(pid).create_time()
        self.loaded = modules(pid)
        verify_files(self.loaded)
        self.reader = Reader(pid)
        try:
            self.call = locate(self.reader, self.loaded)
            self.read()
        except Exception:
            self.close()
            raise

    def read(self):
        if psutil.Process(self.pid).create_time() != self.created:
            raise VoiceError('微信已重新启动，请重新读取。', 'no_call')
        if locate(self.reader, self.loaded) != self.call:
            raise VoiceError('通话已变化，请重新读取并应用。', 'no_call')
        values = parameters(self.reader, self.call)
        if locate(self.reader, self.loaded) != self.call:
            raise VoiceError('通话已结束，请重新读取。', 'no_call')
        return values

    def close(self):
        self.reader.close()


class Engine:
    def __init__(self):
        self.probe = None
        self.path = ''
        self.session = None
        self.script = None
        self.last_message = ''

    def _find(self, path):
        if not path:
            raise VoiceError('未找到微信，请点击“选择微信”。', 'not_found')
        all_processes = running_processes()
        matches = [p for p in all_processes if p.get('exe') and path_key(p['exe']) == path_key(path)]
        if not matches:
            if any(not p.get('exe') for p in all_processes):
                raise VoiceError('无法读取微信进程，请让微信和本工具使用相同的权限级别。', 'permission')
            raise VoiceError('请先打开所选微信，并接通电脑语音。', 'not_running')
        found, errors = [], []
        for process in matches:
            try:
                found.append(Probe(process['pid']))
            except VoiceError as exc:
                errors.append(exc)
            except (PermissionError, psutil.AccessDenied) as exc:
                errors.append(VoiceError('权限不足，请让微信和本工具使用相同的权限级别。', 'permission'))
            except (OSError, ValueError, psutil.Error):
                errors.append(VoiceError('通话未就绪，请接通后重新读取。', 'no_call'))
        if len(found) > 1:
            for probe in found:
                probe.close()
            select_call(found)
        if found:
            return found[0]
        for kind in ('unsupported', 'ambiguous', 'permission'):
            match = next((e for e in errors if e.kind == kind), None)
            if match:
                raise match
        raise VoiceError('请先接通电脑微信语音，再读取状态。', 'no_call')

    def read(self, path):
        if self.path != path:
            self.close()
            self.path = path
        changed = False
        if self.probe:
            try:
                values = self.probe.read()
                if self.script:
                    stats = self.script.exports_sync.stats()
                    if not stats['enabled'] or not stats['same_call']:
                        self.restore()
                        changed = True
                return dict(values, holding=self.script is not None, pid=self.probe.pid,
                            call_key=f'{self.probe.pid}:{self.probe.call.identity[-1]}', changed=changed)
            except Exception:
                was_holding = self.script is not None
                self.close()
                self.path = path
                changed = was_holding
        self.probe = self._find(path)
        return dict(self.probe.read(), holding=False, pid=self.probe.pid,
                    call_key=f'{self.probe.pid}:{self.probe.call.identity[-1]}', changed=changed)

    def apply(self, path):
        current = self.read(path)
        if current['holding']:
            return current
        # Revalidate disk and call identity immediately before creating a hook.
        verify_files(modules(self.probe.pid))
        self.probe.read()
        cfg = {'identity': [hex(x) for x in self.probe.call.identity],
               'channel_index': self.probe.call.index, 'channel_id': self.probe.call.channel_id,
               'target': 128000}
        source = Path(__file__).with_name('hook.js').read_text(encoding='utf-8')
        source = source.replace('__CONFIG__', json.dumps(cfg))
        try:
            self.session = frida.attach(self.probe.pid)
            self.script = self.session.create_script(source)
            self.script.load()
            self.script.exports_sync.start()
            result = self.read(path)
            if not result['holding'] or result['target_bps'] != 128000:
                raise VoiceError('应用后回读不一致，已恢复默认，请重新读取。')
            return result
        except Exception as exc:
            self.restore()
            if isinstance(exc, VoiceError):
                raise
            raise VoiceError('未能应用：通话已变化、权限不足或存在其他增强工具；请关闭其他工具后重试。') from exc

    def restore(self):
        script, session = self.script, self.session
        self.script = self.session = None
        result, failure = None, None
        try:
            if script:
                result = script.exports_sync.stop()
                if result.get('same_call') and (result.get('restore_result') != 0 or
                        result.get('opus_requested_bps') != result.get('wrapper_bps')):
                    failure = VoiceError('增强已停止，但默认码率回读未确认；请重新接通语音。')
        except frida.InvalidOperationError:
            pass  # Target exited/detached: Frida has already removed its hooks.
        except Exception as exc:
            failure = VoiceError('增强已停止，恢复状态无法确认；请重新接通语音。')
        finally:
            if session:
                try:
                    session.detach()
                except frida.InvalidOperationError:
                    pass
        if failure:
            raise failure
        return result

    def close(self):
        try:
            return self.restore()
        finally:
            if self.probe:
                self.probe.close()
                self.probe = None
