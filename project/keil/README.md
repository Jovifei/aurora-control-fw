# Keil 工程

1. 安装 `Geehy.G32F031_DFP.1.0.1` 或兼容的新版本设备包。
2. 用 Keil MDK 打开 `AuroraControl.uvprojx`。
3. 工具链选择 ARM Compiler 6，执行 `Rebuild All`。
4. 必须检查 `.map`：代码区不能进入 `0x0000FC00～0x0000FFFF`，该区由内部 Flash 双页参数存储使用。
5. 当前 `BOARD_POWER_OUTPUT_ALLOWED=0`，不得为了消除“无PWM”而改为1。

Host 测试通过不能替代本工程的 ARM Compiler 6 链接、下载和台架波形验证。
