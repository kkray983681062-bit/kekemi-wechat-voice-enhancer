# 原生轻量版 v1.1.4

Win32/GDI 界面，内嵌 MinHook 组件，Opus 128 / SILK 100 kbps。
支持多开微信的实例选择与独立保持；后台持续发现新实例。

使用与构建见 [guide.md](../docs/guide.md)，本轮验证见 [VALIDATION.md](VALIDATION.md)。
接口识别规则见 [compatibility-signatures.md](../docs/compatibility-signatures.md)。

`render_preview.c` 使用正式界面的绘制代码和演示数据生成说明图，不访问运行中的微信。
测试、说明图生成器与研究程序均不包含在交付 EXE 内。
