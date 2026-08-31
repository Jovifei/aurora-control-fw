# Aurora Control Firmware

> 当前候选基线：**v0.10.3**。在 v0.10.2 真实源码集成基础上，关闭快速 Break 误判、Relay 关波后新鲜采样、Relay 执行反馈、Demo Relay 和陈旧能量累计等软件缺口；所有物理功率门仍保持关闭。

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
docs/        工程事实、设计边界、验证状态与台架清单
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
power_stage  预充、Relay Hold-off、继电器和物理Duty执行器
ui           RUN/FAULT指示逻辑
protocol     旧产品UART帧兼容层
storage      片内Flash双页Journal、CRC和Commit Marker
```

仓库不包含旧MCU工程、闭源MPPT库、`legacy_*`、`tasks/`、重复的`firmware/tests/tools`、应用目录内厂商例程、生成JSON或bootstrap临时文件。

## 阅读入口

- [文档入口](docs/README.md)
- [工程接手指南](docs/GUIDE.md)
- [参数标定与Codex交接清单](docs/17-参数标定与Codex交接清单.md)
- [120W V2.7行为与参数基线](docs/22-REF-120W-V2.7行为与参数基线.md)
- [120W到300W行为迁移矩阵](docs/23-120W到300W行为迁移矩阵.md)
- [v0.10.2实现说明](docs/41-v0.10.2-成熟行为二次审计与实现说明.md)
- [Demo安全边界](docs/42-v0.10.2-Demo无电池带载模式与安全边界.md)
- [Keil AC6修复与发布边界](docs/43-v0.10.2-Keil_AC6修复与发布边界.md)
- [v0.10.2功能移植审核](docs/44-v0.10.2-120W到300W功能移植审核报告.md)
- [v0.10.3安全握手与快故障修复](docs/45-v0.10.3-安全握手与快故障修复说明.md)

## 本地验证

```bash
python tools/run_checks.py
```

该命令依次执行：

```text
Architecture gate
Code style/comment/layout gate
Python contract tests
GCC strict build + CTest
Clang strict build + CTest
Clang AddressSanitizer + UBSan
Cortex-M0+ target syntax check
```

本地若缺少某个编译器，脚本会在输出中明确显示 `skip`；正式发布结论应以工具齐全的 GitHub Actions、Keil ARM Compiler 6日志和MAP审计共同为准。Host通过不等于功率板验收通过。

MCU弱光启动遵循：最小安全GPIO → PVD Ready → VDD连续稳定 → `aurora_runtime_init()`。PVD Reset/IRQ保持关闭；供电不足时只等待，不创建APP故障或启动IWDT复位循环。

## IDE / clangd 函数跳转

clangd 需要本机生成 `compile_commands.json`（已在 `.gitignore` 中，不进仓库）。

```powershell
.\.tools\generate-clangd-db.cmd
```

或在任意平台执行：

```bash
python tools/generate_clangd_db.py
```

生成后请在 Cursor 中执行 `Clangd: Restart language server`。

配套文件：

```text
compile_flags.txt              全工程兜底解析参数（未收录进 Keil 工程的文件也会用到）
.clangd                        后台索引配置
.clangd-support/include/       裸机解析用最小 stdio stub
.tools/generate-clangd-db.cmd  Windows 一键生成 compile_commands.json
```

要求：

1. 安装 Cursor/VS Code 扩展 `clangd`（LLVM 发布），并禁用或关闭 Microsoft `C/C++` IntelliSense。
2. 本机 PATH 中有 `clang`，或已安装 Keil `armclang`（脚本会自动附加其系统头路径）。
3. 修改 Keil 工程源文件列表或 include/define 后，重新运行生成脚本。

## 当前安全状态

所有功率放行门保持关闭：

```c
BOARD_GATE_COMP_ROUTE_VALIDATED == 0
BOARD_GATE_ANALOG_CALIBRATED    == 0
BOARD_GATE_KEIL_LINKED          == 0
BOARD_GATE_LOW_VOLTAGE_BENCH    == 0
BOARD_GATE_DEMO_LOAD_VALIDATED  == 0
BOARD_POWER_OUTPUT_ALLOWED      == 0
```

真正解锁前必须完成 `docs/11-Keil编译与台架验收.md` 的Keil MAP、模拟标定、COMP/Break强制触发、首脉冲/Vgs/电感电流、Relay拉弧、Demo负载和低压到额定功率验收。

## v0.10.3 发布边界

v0.10.3 只能定义为：

```text
SOFTWARE SAFETY FIXED
READY FOR LOW-VOLTAGE BENCH
POWER GATES STILL LOCKED
OTA/IAP OUT OF SCOPE
```

本版本不修改3A CC、12A PV限流、BST_U分压BOM或既有30字节遥测字段语义；也不新增OTA/IAP代码。
