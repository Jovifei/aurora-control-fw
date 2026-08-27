# Codex / Agent 工作约束

1. 先读 `docs/00-文档索引.md`、`02-软件分层与依赖规则.md`、`07-保护PWM与看门狗安全设计.md` 和 `15-重构清理与验证报告.md`。
2. 不得重新引入旧 MCU 源码、闭源 MPPT 库、完整 SDK 示例、`legacy_*`、`tasks/`、重复目录或生成 JSON。
3. `app/`、`service/`、`driver/`、`board/` 均保持扁平；APP `.c` 总数不得无理由超过 10 个。
4. APP不得包含芯片、Driver、Board或Service头文件；Driver不得包含业务头文件。
5. ADC必须保持：ATMR定时触发 → 一次六通道扫描 → DMA双块 → ISR只发布块 → 主循环换算和发布快照。
6. MPPT只输出PV参考电压和理论功率请求，不得操作CCR、GPIO或MOE。
7. 正常PWM只有Service一个出口；故障ISR只允许关波、锁存和投递事件，不能恢复。
8. CCR必须使用preload，在自然UEV生效；运行期禁止软件UG。UPDATE中断只允许在首次0占空比装载时临时开启一次。
9. COMP0_O通过PB10/AF7接门极驱动EN，按低有效故障处理；在台架确认前保持对应门禁关闭。
10. 只有Service健康监督可以喂IWDT；主循环或ISR不得直接喂狗。
11. 功率运行或继电器闭合时禁止片内Flash擦写。
12. 始终保持 `BOARD_POWER_OUTPUT_ALLOWED=0`，除非已有Keil map、低压台架、比较器强制触发和示波器证据，并经人工审查。
13. 每次修改后运行 `python tools/run_checks.py`；不得通过降低警告、删除测试或放宽架构规则使结果变绿。
