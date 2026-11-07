# 第三方组件

当前轻量版使用 MinHook 1.3.4，来源为 [TsudaKageyu/minhook](https://github.com/TsudaKageyu/minhook/tree/v1.3.4)。
源码位于 `native/vendor/minhook/`，原始许可完整保留在 [LICENSE.txt](native/vendor/minhook/LICENSE.txt)，
包含 MinHook 与其反汇编组件的许可声明。EXE 的 202 号 RCDATA 资源也内嵌该许可。

EXE 使用 MinGW-w64 编译，只依赖 Windows 系统 DLL，不包含 Python、Frida、Pillow 或 PyInstaller。
项目头像与图标来自用户提供的图片，相关权利属于原权利人。本仓库不包含微信程序或引擎 DLL。
