# Aurora Control Firmware

单路异步 Boost 光伏充电控制器的可移植嵌入式固件。当前代码基线采用清理后的量产工程布局：默认高功率 BOM，可编译切换低功率 BOM，支持 48/60/72 V 与四类电池档案。

## 目录

```text
app/       纯业务模块，按功能合并为 9 组 .c/.h
service/   中断邮箱、主循环服务和 APP↔Driver 唯一桥接
board/     引脚、极性、ADC比例、Flash地址和硬件门禁
driver/    目标芯片驱动，全部扁平放置
vendor/    仅实际构建所需的 CMSIS / Device / DDL
project/   Keil ARM Compiler 6 工程、入口和中断文件
docs/      00～16 编号文档与最终原理图
tests/     Host 回归与故障注入，不进入目标镜像
tools/     架构、GCC/Clang、目标端语法门禁，不进入目标镜像
```

`app/` 只有以下功能模块：

```text
app          应用编排与协议命令落地
measurement  ADC完整块滤波、标定、物理量快照和电池电流估算
mppt         自研P-V斜率搜索、PV参考电压与PI功率请求
charger      电池档案及TC/CC/CV/Float状态机
protection   软件保护、去抖、锁存与恢复许可
power_stage  预充、继电器、功率命令和物理Duty执行器
ui           RUN/FAULT指示逻辑
protocol     旧产品UART帧兼容层
storage      片内Flash双页记录格式、CRC和提交标记
```

仓库不包含旧 MCU 工程、旧闭源 MPPT 库、`legacy_*`、`tasks/`、应用目录内厂商例程、重复的 `firmware/tests/tools` 或生成 JSON。

## 本地验证

```bash
python tools/check_architecture.py
python tools/run_checks.py
```

`tests/` 和 `tools/` 是必要的开发质量门禁，但 Keil 工程不会编译它们。它们用于阻止 APP 越层访问寄存器、PWM 被多处控制、ADC 缓冲覆盖、看门狗盲喂、旧源码回流等问题。

## 当前安全状态

所有功率放行门默认关闭：

```c
BOARD_POWER_OUTPUT_ALLOWED == 0
```

Host 测试和目标端语法检查通过，不等于已经完成 Keil AC6 链接和功率板验收。真正解锁前必须完成 `docs/11-Keil编译与台架验收.md` 中的检查。
