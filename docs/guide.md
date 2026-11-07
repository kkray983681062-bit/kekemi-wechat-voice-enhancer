# 使用与构建

## 使用

下载 Release 中的 `Kekemi-WeChat-Voice-Enhancer.exe`，打开后接通电脑微信语音。
点击“读取当前状态”，再点击“应用”。工具按实际编码器选择 Opus 128 或 SILK 100 kbps。

多开时，用右上方“微信实例”按钮选择对应进程。每个实例的码率、曲线和保持状态独立；
可以依次给两个实例应用，切换查看时其他实例仍继续保持。“恢复默认”只停止当前实例，
关闭工具则停止全部实例。实例编号和 PID 用于区分同一路径启动的微信，不读取账号或聊天内容。
后台每两秒查找新实例；微信更换通话后需重新应用。第二个微信没有通话时显示等待通话。

自动识别安装位置，也能选择并记住 `Weixin.exe` 或 `WeChat.exe`。安装路径不再限制只能发现一个实例。
窗口支持拖动边缘缩放、双击标题栏放大与还原。

## 提示与数值

- “等待语音编码”：Opus 目标已经设置，开麦产生新语音帧后更新编码回读。
- SILK 需要开麦产生编码帧，确认编码成功后开始保持。
- “等待通话”：该实例尚未有可识别的语音通话，可在实例菜单切换到另一个微信。
- “接口未识别”：当前引擎结构与识别规则不兼容，需要进一步适配。
- “权限不足”：让微信和增强工具在相同权限下运行。

曲线和 kbps 表示编码器内部目标，不是网络实测速率或音质分数。工具不修改采样率、声道，
不读取或保存通话音频、聊天记录，不上传运行信息，也不修改微信安装文件。

已在微信 4.2.0.13 x64 验证。识别结合函数特征、实际调用关系和 RTTI 类型，
并动态定位通话对象；兼容的更新可以继续使用，但不能保证所有未来版本。
点击“应用”才加载内嵌原生组件。停止后组件禁用，随微信退出释放，避免音频线程返回到已卸载代码。

## 构建

使用 MinGW-w64 GCC 和 windres。Linux 交叉编译环境在仓库根目录运行：

```sh
sh native/build.sh
```

Windows 的 MSYS2 MinGW64 环境可指定工具名称：

```sh
CC=gcc WINDRES=windres sh native/build.sh
```

输出为 `native/build/Kekemi-WeChat-Voice-Enhancer.exe`。EXE 内嵌组件、图标和 MinHook 许可，
无需额外 DLL 或 Python 运行时。`NATIVE_OUT` 可指定相对于 `native/` 的输出路径。

在 Windows 上运行 `tests-core.exe`、`tests-clients.exe` 和 `tests-shim.exe`。
`tests-compat.exe`、`tests-codec-update.exe`、`tests-payload-update.exe` 依次接收本机
4.2.0.13 的 `VoipEngine.dll` 和 `ConfSdk.dll` 完整路径；后两者仅在测试进程内处理合成音频。
本仓库不附带微信 DLL。

## 源码

- `native/ui.c`、`clients.c`：界面、实例选择和独立状态。
- `native/core.c`、`discovery.c`、`compat.c`：进程枚举、通话对象和接口识别。
- `native/bridge.c`、`payload.c`、`opus_shim.S`：加载原生组件、保持与恢复。
- `native/tests_*.c`：对象变化、多实例、调用约定、真实编码器与保持模块检查。

[第三方组件](../THIRD_PARTY_NOTICES.md)。本项目与腾讯、微信官方无关联。
