# Aurora Control Firmware

> 工程整理发布：**v0.7.2**。本版本继续统一应用层与驱动层目录，并记录编译修复后的安全审计结果。

单路异步 Boost 光伏充电控制器的可移植嵌入式固件。当前基线默认高功率BOM，可编译切换低功率BOM，支持48/60/72V与铅酸、三元锂、磷酸铁锂、钠离子四类电池档案。

## 目录

```text
app/
├─ inc/      应用层公共.h、类型、参数和运行时接口
└─ src/      应用逻辑、主入口、中断桥接和Debug实现
driver/
├─ inc/      驱动模块头、板级配置和公共驱动契约
└─ src/      G32F031外设及板级驱动实现
vendor/      目标构建实际需要的CMSIS / Device / DDL
project/     Keil ARM Compiler 6工程、scatter和用户工程配置
docs/        文档入口、00～19编号文档和最终原理图
tests/       Host回归与故障注入，不进入目标镜像
tools/       架构、代码规范、GCC/Clang、Sanitizer和目标语法门禁
```

`app/src/`中的应用模块：

```text
main         应用入口、运行时调度、事件邮箱和安全功率提交
interrupts   中断向量与轻量事件桥接
debug        GE_DEBUG日志及编译期模块开关
measurement  ADC完整块滤波、标定、物理量快照和电池电流估算
mppt         P-V斜率搜索、PV参考电压和PI功率请求
charger      电池档案与TC/CC/CV/Float状态机
protection   软件保护、去抖、锁存与恢复许可
power_stage  预充、继电器、功率命令和物理Duty执行器
ui           RUN/FAULT指示逻辑
protocol     旧产品UART帧兼容层
storage      片内Flash双页Journal、CRC和Commit Marker
```

仓库不包含旧MCU工程、闭源MPPT库、`legacy_*`、`tasks/`、重复的`firmware/tests/tools`、应用目录内厂商例程、生成JSON或bootstrap临时文件。

## 阅读入口

- [文档入口](docs/README.md)
- [工程接手指南](docs/GUIDE.md)
- [参数标定与Codex交接清单](docs/17-参数标定与Codex交接清单.md)
- [v0.7.2目录规范与交接说明](docs/18-v0.7.2目录规范与交接说明.md)
- [编译修复提交2740523审计](docs/19-编译修复提交2740523审计.md)

## 本地验证

```bash
python tools/run_checks.py
```

该命令依次执行；缺少GCC/Clang/CMake/CTest时会明确返回不完整，不会把跳过的Host构建伪装为通过：

```text
Architecture gate
Code style/comment/layout gate
GCC strict build + CTest
Clang strict build + CTest
Clang AddressSanitizer + UBSan
Cortex-M0+ target syntax check
```

Host回归、目标语法检查和Keil链接结果以本次交接报告及最新验证命令输出为准；Host通过不等于功率板验收通过。

## 当前安全状态

所有功率放行门保持关闭：

```c
BOARD_POWER_OUTPUT_ALLOWED == 0
```

真正解锁前必须完成 `docs/11-Keil编译与台架验收.md` 的Keil MAP、模拟标定、COMP/Break强制触发、首脉冲/Vgs/电感电流和低压到额定功率验收。
