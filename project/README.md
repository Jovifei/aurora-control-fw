# Keil 工程

1. 安装 `Geehy.G32F031_DFP.1.0.1` 或兼容的新版本设备包。
2. 用 Keil MDK 打开 `project/AuroraControl.uvprojx`。
3. 工具链选择 ARM Compiler 6，执行 `Rebuild All`。
4. 必须检查 `.map`：代码区不能进入 `0x0000FC00～0x0000FFFF`，该区由内部 Flash 双页参数存储使用。
5. 当前 `BOARD_POWER_OUTPUT_ALLOWED=0`，不得为了消除“无PWM”而改为1。

默认构建使用PA10/PA11连接蓝牙。需要PB7/PB8输出`[GE_DEBUG]`日志时，使用ARM Compiler 6编译宏：

```text
BOARD_USART_MODE=BOARD_USART_MODE_DEBUG
DEBUG_ENABLE=1
```

Debug模式与蓝牙模式独占USART，并关闭产品协议解析和主动遥测。

Host 测试通过不能替代本工程的 ARM Compiler 6 链接、下载和台架波形验证。

目标入口、应用运行时和中断桥接位于 `app/src/main.c` 与 `app/src/interrupts.c`；本目录只保存 Keil 工程、scatter 和用户工程配置。
