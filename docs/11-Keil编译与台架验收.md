# 11 · Keil编译与台架验收

软件证据必须分级：Host测试、ARM Compiler 6链接、下载运行、低压台架、额定功率分别记录。

最低验收顺序：

1. Keil AC6 `Rebuild All`，0 error；检查代码不进入0xFC00以上。
2. 上电默认GLC、GHC、RELAY均为安全状态。
3. 确认ATMR只输出单路GLC，Duty修改只在下一自然UEV生效。
4. 限流低压下确认首脉冲、预充、BST_U/BAT_U压差和继电器吸合。
5. 强制COMP0/COMP2，测量故障到U6 EN/GLC/Vgs关断延迟；持续故障时软件不能重开发波。
6. 校准PV_U、PV_I、BAT_U、BST_U和两路NTC。
7. 逐级提升功率，验证电感峰值、MOS/二极管温升、母线尖峰和EMI。

门禁未完成前保持 `BOARD_POWER_OUTPUT_ALLOWED=0`。
