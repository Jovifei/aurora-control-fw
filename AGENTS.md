# Codex / Agent 工作约束

1. 先读 `docs/README.md`、`docs/GUIDE.md`、`docs/17-参数标定与Codex交接清单.md`、`docs/07-保护PWM与看门狗安全设计.md`、`docs/18-v0.7.2目录规范与交接说明.md` 和 `docs/19-编译修复提交2740523审计.md`。
2. 不得重新引入旧MCU源码、闭源MPPT库、完整SDK示例、`legacy_*`、`tasks/`、`.bootstrap`、重复目录或生成JSON。
3. APP必须采用 `app/inc/*.h` 与 `app/src/*.c`；`app/`根目录不得放C/H文件，应用模块、入口、中断桥接和Debug均按一一对应文件维护。
4. 生产代码只保留`app/`和`driver/`两层；两层均使用`inc/`与`src/`分离，APP可调用Driver契约，Driver不得包含APP业务头文件。
5. 所有函数定义必须使用统一 `Name/Input/Output/Description` 头注释；复杂判断和执行分支应写明原因、安全动作和状态变化。
6. 统一4空格缩进，禁止Tab和行尾空白；所有文件级 `static` 定义集中在首个公开函数之前；宏必须先定义再使用并注明单位/用途。
7. ADC必须保持：ATMR定时触发 → 六通道扫描 → DMA双半块 → ISR只发布 → 主循环换算并整体发布快照。
8. MPPT只输出PV参考电压和理论功率请求，不得操作CCR、GPIO、MOE或Break。
9. 正常PWM只有`app/src/main.c`中的应用运行时一个出口；故障ISR只允许立即关波、锁存和投递事件，不能恢复输出。
10. CCR使用preload并在自然UPDATE边界生效；运行期禁止软件UPDATE。UPDATE中断只允许首次零CCR握手临时开启一次。
11. COMP0_O通过PB10/AF7接门极驱动EN，按低有效故障处理；台架确认前相应门禁保持0。
12. 只有应用运行时健康监督可喂IWDT；功率运行或继电器闭合时禁止Flash擦写。
13. 修改参数前查 `docs/17`，记录旧值、新值、单位、依据、测试和回退值；不得在函数体内新增魔法控制数。
14. 始终保持 `BOARD_POWER_OUTPUT_ALLOWED=0`，除非Keil MAP、模拟标定、比较器强制触发、低压台架和示波器证据均已人工审查。
15. 每次修改后运行 `python tools/run_checks.py`；不得通过降低警告、删除测试、关闭Sanitizer或放宽规则使结果变绿。
