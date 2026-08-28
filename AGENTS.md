# Codex / Agent 工作约束

1. 先读 `docs/README.md`、`docs/GUIDE.md`、`docs/17-参数标定与Codex交接清单.md`、`docs/07-保护PWM与看门狗安全设计.md`、`docs/18-v0.7.2目录规范与交接说明.md`、`docs/19-编译修复提交2740523审计.md`，然后阅读 `docs/22`~`docs/29` 的v0.8.x迁移与供电资格资料。
2. 不得重新引入旧MCU源码、闭源MPPT库、完整SDK示例、`legacy_*`、`tasks/`、`.bootstrap`、重复目录或生成JSON。
3. APP必须采用 `app/inc/*.h` 与 `app/src/*.c`；`app/`根目录不得放C/H文件，APP `.c` 总数不得无理由超过10个。
4. Driver必须采用 `driver/inc/*.h` 与 `driver/src/*.c`；APP不得包含Chip/Driver/Board/Service头文件，Driver不得包含业务头文件。
5. 所有函数定义必须使用统一 `Name/Input/Output/Description` 头注释；复杂判断和执行分支应写明原因、安全动作和状态变化。
6. 统一4空格缩进，禁止Tab和行尾空白；所有文件级 `static` 定义集中在首个公开函数之前；宏必须先定义再使用并注明单位/用途。
7. ADC必须保持：ATMR定时触发 → 六通道扫描 → DMA双半块 → ISR只发布 → 主循环换算并整体发布快照。
8. MPPT只输出PV参考电压和理论功率请求，不得操作CCR、GPIO、MOE或Break。
9. 正常PWM只有Service一个出口；故障ISR只允许立即关波、锁存和投递事件，不能恢复输出。
10. CCR使用preload并在自然UPDATE边界生效；运行期禁止软件UPDATE。UPDATE中断只允许首次零CCR握手临时开启一次。
11. COMP0_O通过PB10/AF7接门极驱动EN，按低有效故障处理；台架确认前相应门禁保持0。
12. 只有Service健康监督可喂IWDT；功率运行或继电器闭合时禁止Flash擦写。
13. 修改参数前查 `docs/17`、`docs/22`、`docs/23`、`docs/26` 和 `docs/28`，记录旧值、新值、单位、依据、测试和回退值；不得在函数体内新增魔法控制数。
14. 始终保持 `BOARD_POWER_OUTPUT_ALLOWED=0`，除非Keil MAP、模拟标定、比较器强制触发、低压台架和示波器证据均已人工审查。
15. 每次修改后运行 `python tools/run_checks.py`；不得通过降低警告、删除测试、关闭Sanitizer或放宽规则使结果变绿。

## v0.8.0 Protection & Charge Behavior Parity

- 120W V2.7只提供成熟产品行为；300W功率限流、过功率和热降额曲线需重新台架冻结。
- MOS从95°C开始降额；105°C/1s保护、95°C/1s恢复。
- Relay硬约束：`PRECHARGE Relay OFF → Boost充BST_U → |BST_U-BAT_U|<=1.5V持续1s → PWM OFF → Service实时复核 → Relay ON → 100ms复核 → BAT_U 10s稳定 → RUN`。
- 任何“先Relay ON，再慢慢把BST_U升到BAT_U”的实现都属于安全回归。
- 无BAT_I硬件，任何电池电流都必须标识为ESTIMATED。
- APP不得直接操作Driver；CMP/Break/CCR/MOE最终动作仍由Service和Driver负责。

## v0.8.1 MCU Supply Qualification

- PVD只用于“是否允许继续完整初始化”的启动资格，不属于APP运行期保护。
- 启动顺序必须保持：`System/SysTick → 最小GPIO安全态 → PVD资格等待 → Service完整初始化`。
- `BOARD_MCU_PVD_THRESHOLD_MV`、`BOARD_MCU_PVD_FILTER_US`、`BOARD_MCU_PVD_READY_TIMEOUT_US`、`BOARD_MCU_SUPPLY_STABLE_TIME_MS`必须集中在`board/board_config.h`，不得在Driver函数体内写死。
- 弱光导致`PVDSTS=LOW`时只能重新累计稳定时间并继续等待，不得产生`AURORA_FAULT_*`、不得启动IWDT、不得触发软件复位。
- `BOARD_MCU_PVD_RESET_ENABLE`和`BOARD_MCU_PVD_IRQ_ENABLE`必须保持0；目标Driver必须继续显式关闭`PVDRSTEN`与PVD中断。
- PVD模块自身`PVDRDY`超时才视为启动硬件异常；这与“VDD暂时低”严格区分。
- 供电资格通过后关闭PVD，运行期弱光继续由PV_U/PV功率/启动状态机处理；不要重新引入“MCU弱光保护”。
