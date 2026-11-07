"""Application entry point. Does not apply changes until the user clicks Apply."""
import ctypes
import sys


def main():
    kernel = ctypes.WinDLL('kernel32', use_last_error=True)
    kernel.CreateMutexW.argtypes = [ctypes.c_void_p, ctypes.c_bool, ctypes.c_wchar_p]
    kernel.CreateMutexW.restype = ctypes.c_void_p
    kernel.CloseHandle.argtypes = [ctypes.c_void_p]
    handle = kernel.CreateMutexW(None, False, r'Local\KekemiWeChatVoiceEnhancer-v1')
    if not handle:
        raise ctypes.WinError(ctypes.get_last_error())
    if ctypes.get_last_error() == 183:
        ctypes.windll.user32.MessageBoxW(None, '工具已经打开，请使用已打开的窗口。', '克克咪 微信语音增强', 0x40)
        kernel.CloseHandle(handle)
        return
    try:
        ctypes.windll.shell32.SetCurrentProcessExplicitAppUserModelID('Kekemi.WeChatVoiceEnhancer.1')
        from wevoice.gui import App
        App().run()
    except Exception:
        import traceback
        from wevoice.locator import settings_path
        folder = settings_path().parent
        folder.mkdir(parents=True, exist_ok=True)
        (folder/'error.log').write_text(traceback.format_exc(), encoding='utf-8')
        ctypes.windll.user32.MessageBoxW(None, '程序启动失败。诊断信息已保存在本地设置目录的 error.log。',
                                       '克克咪 微信语音增强', 0x10)
        sys.exit(1)
    finally:
        kernel.CloseHandle(handle)


if __name__ == '__main__':
    main()
