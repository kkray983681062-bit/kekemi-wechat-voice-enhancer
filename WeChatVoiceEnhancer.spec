from PyInstaller.utils.hooks import copy_metadata

datas = [('assets', 'assets'), ('wevoice/hook.js', 'wevoice')]
datas += copy_metadata('frida')
a = Analysis(['launch.py'], pathex=[], binaries=[], datas=datas,
             hiddenimports=['frida._frida', 'PIL._tkinter_finder'],
             hookspath=[], hooksconfig={}, runtime_hooks=[],
             excludes=['numpy', 'pandas', 'matplotlib', 'scipy', 'IPython', 'pytest'])
pyz = PYZ(a.pure)
exe = EXE(pyz, a.scripts, a.binaries, a.datas, [],
          name='Kekemi-WeChat-Voice-Enhancer', debug=False,
          bootloader_ignore_signals=False, strip=False, upx=False,
          console=False, disable_windowed_traceback=False,
          icon='assets/app.ico')
