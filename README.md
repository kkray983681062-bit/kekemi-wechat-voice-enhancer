# 克克咪 微信语音增强

Windows 桌面工具：读取当前微信语音通话的编码目标，一键应用并持续保持 **128 kbps**。提供最近 30 秒的编码目标曲线、自动定位并记住微信安装位置、手动选择位置和恢复默认。

**[下载最新版](https://github.com/kkray983681062-bit/kekemi-wechat-voice-enhancer/releases/latest)**

## 下载与使用

普通用户下载 Release 中的 `Kekemi-WeChat-Voice-Enhancer.exe`，或者包含说明和组件许可的 Windows x64 ZIP。无需安装 Python。GitHub 自动提供的 `Source code` 是源码，不是可直接运行的程序。

1. 打开电脑微信，接通语音通话。
2. 双击增强工具。工具会自动查找并记住微信位置；找不到时点击 **选择微信**，选择微信的 `Weixin.exe` 或 `WeChat.exe`。
3. 点击 **读取当前状态**，确认已读到当前通话。
4. 点击 **应用 128 kbps**，状态变为“增强保持中”。
5. 点击 **恢复默认**，或正常关闭工具，停止增强并恢复微信当前的默认目标。

增强只作用于本次通话。通话对象变化时停止保持；新通话需要重新点击应用。不要同时运行多个增强工具。

## 当前适配范围

- Windows x64；本次在 Windows 11 上验证，发布前已由使用者实际测试确认。
- 微信 Windows **4.1.15.56 x64**，并且三个引擎文件必须匹配下方指纹。仅版本号相同并不足以保证兼容。
- 当前已验证的 Opus 语音通话路径。多实例存在多个候选通话时，不会随意选一个修改。
- 不支持其他未验证版本；程序会明确提示并停止应用。微信更新后可能需要重新适配。

| 已加载模块 | SHA-256 |
| --- | --- |
| Weixin.dll | `dcd309bf5a0cee9219d12ee7308f706b77ad798393c9230f904db5a4ce861c11` |
| VoipEngine.dll | `ec7cfdbc32842111be3287753832559c0403a79a3042a3a9ae57dd3908bc225c` |
| ConfSdk.dll | `fd3bfa52b84a19726f30273e6bf52f6e99a513077707bf1ee5cf87ca24f98c2a` |

## 数值含义与工作方式

界面的数值和曲线是 **Opus 编码目标**，不是实测网络发送速率。采样率、声道数与 kbps 是不同参数；本工具仅将目标码率设为 128 kbps，不保证对端收到同样的网络码率，也不承诺音质必然提升。

应用时，工具验证已加载引擎的文件指纹、原生函数签名和当前通话身份，再通过 Frida `Interceptor` 拦截当前编码器的 `OPUS_SET_BITRATE` 调用，将该实例的目标参数设为 128000。只匹配当前实例及码率命令，轮询用于读取状态和确认通话身份，不反复写入码率字段。

恢复时撤销拦截，使用微信包装层保留的最新默认值。不会修改微信安装文件，不读取聊天内容、不录音、不发起通话，也不上传运行数据。安装位置保存在当前用户的 `%LOCALAPPDATA%\KekemiWeChatVoiceEnhancer\settings.json`；启动错误日志只保存在同目录的 `error.log`。

## 常见提示

- **未找到微信**：点击“选择微信”，选择 64 位微信主程序。
- **请先打开所选微信**：当前选择的位置与运行中的微信应一致。
- **请先接通电脑微信语音**：接通后重新读取。部分未适配的语音路径可能无法定位。
- **当前微信引擎未适配**：版本或文件指纹不匹配，当前版本不能应用；请勿替换 DLL 或强行跳过检查。
- **权限不足 / 未能应用**：让微信和工具使用相同的权限级别，并关闭其他增强工具。
- **工具已经打开**：先关闭旧窗口，再打开新版本。

## 从源码构建

使用 Windows x64、Python 3.11 x64。依赖版本固定在 `requirements-build.txt`。

```powershell
py -3.11 -m venv .venv
.\.venv\Scripts\python.exe -m pip install -r requirements-build.txt
.\.venv\Scripts\python.exe -m unittest discover -s tests -v
.\.venv\Scripts\python.exe -m PyInstaller --noconfirm WeChatVoiceEnhancer.spec
```

也可运行根目录的 `build.ps1`。输出为 `dist/Kekemi-WeChat-Voice-Enhancer.exe`。直接运行源码：`.\.venv\Scripts\python.exe launch.py`。

## 源码结构

- `wevoice/gui.py`：中文深色界面和最近 30 秒曲线。
- `wevoice/locator.py`：自动查找、手动选择和本地路径保存。
- `wevoice/core.py`、`wevoice/winmem.py`：版本验证和只读参数定位。
- `wevoice/engine.py`、`wevoice/hook.js`：应用、保持和恢复的生命周期。
- `tests/`：路径保存、未知版本、空通话、多实例及后台读取时点击应用的回归测试。

第三方组件及许可见 [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md) 与 `licenses/`。本项目与腾讯、微信官方无关联。头像为用户提供的界面素材，相关权利属于原权利人。
