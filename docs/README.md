# Aurora Control Firmware 文档入口

本目录是工程事实、设计边界、验证状态和参数交接的唯一文档入口。阅读时必须区分：

- **源码静态事实**：可由当前主线代码直接证明；
- **Host验证事实**：可由GCC/Clang/CTest/Sanitizer复现；
- **目标编译事实**：必须由Keil ARM Compiler 6日志、AXF/HEX/MAP证明；
- **板级事实**：必须由原理图复核、限流台架和示波器波形证明。

推荐顺序：

1. [00-文档索引](00-文档索引.md)
2. [49-当前主线状态与下一步待测试总表](49-v0.10.3-当前主线状态与下一步待测试总表.md)
3. [46-新工程分阶段移植与板级验证路线](46-v0.10.3-新工程分阶段移植与板级验证路线.md)
4. [GUIDE](GUIDE.md)
5. [01-工程概览与目录说明](01-工程概览与目录说明.md)
6. [02-软件分层与依赖规则](02-软件分层与依赖规则.md)
7. [07-保护PWM与看门狗安全设计](07-保护PWM与看门狗安全设计.md)
8. [08-继电器预充与无发电断开](08-继电器预充与无发电断开.md)
9. [17-参数标定与Codex交接清单](17-参数标定与Codex交接清单.md)
10. [22-120W V2.7行为与参数基线](22-REF-120W-V2.7行为与参数基线.md)
11. [23-120W到300W行为迁移矩阵](23-120W到300W行为迁移矩阵.md)
12. [35-BST_U量程风险与硬件整改门禁](35-v0.9.0-BST_U量程风险与硬件整改门禁.md)
13. [41-v0.10.2成熟行为二次审计与实现说明](41-v0.10.2-成熟行为二次审计与实现说明.md)
14. [42-Demo无电池带载模式与安全边界](42-v0.10.2-Demo无电池带载模式与安全边界.md)
15. [43-Keil ARMCLANG修复与发布边界](43-v0.10.2-Keil_AC6修复与发布边界.md)
16. [44-v0.10.2功能移植审核报告](44-v0.10.2-120W到300W功能移植审核报告.md)
17. [45-v0.10.3安全握手与快速故障修复说明](45-v0.10.3-安全握手与快故障修复说明.md)
18. [47-v0.10.3审阅问题补强与验证记录](47-v0.10.3-审阅问题补强与验证记录.md)
19. [48-v0.10.3 Flash边界与停机保存闭环](48-v0.10.3-Flash边界与停机保存闭环.md)
20. [程序代码分析阅读索引](程序代码分析/00-阅读索引.md)

如果目标是**从远端拉取后建立一个新工程，并在真实板上按功能逐项移植**：

```text
先看49号：确认“现在做到哪、还缺什么证据”
→ 再按46号：逐Gate执行测试，不跳级
→ 每一Gate把实测证据写回docs/bringup/Gxx-*/RESULT.md
```

## 当前执行状态摘要

截至当前主线文档同步，仓库已有的证据只支持以下状态：

| Gate | 代码/静态腿 | 板级腿 | 当前结论 |
|---|---|---|---|
| G0 | 文档/PinMap/Flash分区已整理 | 签字、断电通断与误焊检查待做 | `IN_PROGRESS` |
| G1 | 最小安全工程、Keil静态证据已完成 | 实板上电安全态待做 | `IN_PROGRESS` |
| G2 | PVD/时基/UART/IWDT代码与Keil证据完成 | PVD/UART/IWDT/2h实测待做 | `IN_PROGRESS` |
| G3 | ADC raw代码腿完成 | 0~3V、通道映射、BAT_U建立时间待做 | `IN_PROGRESS` |
| G4 | 10kHz TRGO + 6 rank + DMA代码腿完成 | 顺序/stale/overrun/2h待做 | `IN_PROGRESS` |
| G5 | 工程量理论模型已进入代码 | 电压/NTC/PV_I标定与误差签字待做 | `IN_PROGRESS` |
| G6~G15 | 路线和PASS条件已定义 | 无BOARD_PASS证据 | **不得宣称完成** |

下一步应从**G0未闭合的实板/签字项开始**，然后依次补G1→G5的板级证据。G7的COMP/Break硬件保护链没有BOARD_PASS前，严禁进入G8真实传能。

完整状态、测试顺序和待测清单见[49号总表](49-v0.10.3-当前主线状态与下一步待测试总表.md)。

## 当前候选：v0.10.3

v0.10.3在不移植OTA/IAP运行功能、不修改3A CC、12A PV限流和BST_U分压BOM的前提下，已完成以下软件修复：

- COMP/Break依据故障前的软件PWM授权分类，避免硬件先清MOE后误判；
- Battery Relay增加20ms关波放能与更新ADC快照复核；
- PowerStage必须看到Runtime已写Relay GPIO才推进机械稳定或Demo Probe；
- Demo使用独立闭合条件和30W不可绕过功率上限；
- 预充失败不再因低Ppv被误判为弱光；
- 陈旧或ADC故障期间的缓存正功率不再累计PV能量；
- Flash只允许写最后两页Journal，拒绝地址0、越界、跨页和0长度写；
- Flash仅在明确停机态保存，不使用LVD/PVD欠压事件抢写；
- Journal事务只有在Commit-last与完整回读成功后才推进`sequence/active_page`；
- 连续写失败最多重试一次，再失败本次上电禁止继续擦写。

Flash软件闭环的设计与剩余实板断电测试见[48号文档](48-v0.10.3-Flash边界与停机保存闭环.md)。

## 当前生产门禁

任何“可以发波”“已通过硬件保护”“可上额定功率”的结论，都必须同时满足 `docs/11-Keil编译与台架验收.md` 和 `driver/inc/board_config.h` 的全部门禁。当前仍为：

```c
BOARD_GATE_COMP_ROUTE_VALIDATED == 0
BOARD_GATE_ANALOG_CALIBRATED    == 0
BOARD_GATE_KEIL_LINKED          == 0
BOARD_GATE_LOW_VOLTAGE_BENCH    == 0
BOARD_GATE_DEMO_LOAD_VALIDATED  == 0
BOARD_POWER_OUTPUT_ALLOWED      == 0
```

因此当前版本只具备集成到受限Bring-up构建的**软件候选资格**。当前生产配置不能直接作为“解门台架固件”；低压验证应按46号路线使用独立Bring-up构建，只开放当阶段最小能力。

仍需板级留证据的关键项包括：

```text
G0：PinMap/BOM签字、断电通断与误焊检查
G1：复位/上电功率脚绝对安全态
G2：PVD阈值与抖动、UART回环、IWDT、2h稳定
G3：ADC raw与六通道映射、BAT_U建立时间
G4：DMA顺序、stale/overrun、2h连续采样
G5：PV_U/BAT_U/BST_U/NTC/PV_I标定
G6：单路PWM频率、Duty、变Duty、无毛刺
G7：COMP→Break→U6 EN→Q6 Vgs真实快速关断
G8~G12：低压功率级、Relay、保护、MPPT、充电状态机
G13：Flash随机断电注入、协议、LED、24h运行
G14：Demo真实负载矩阵
G15：5W→额定功率、SOA与热设计
```

详细门槛必须以46号路线与各`docs/bringup/Gxx-*/RESULT.md`为准；没有原始波形/CSV/数值证据的项目不能从`IN_PROGRESS`改成`BOARD_PASS`。
