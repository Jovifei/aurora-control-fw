# v0.8.0 分支评审提示

本分支用于评审 Protection & Charge Behavior Parity。请勿在未完成P0硬件台架前合并任何功率解锁改动；当前 `BOARD_POWER_OUTPUT_ALLOWED` 必须保持0。

重点评审：

1. Relay OFF → Boost预充BST_U → 压差稳定 → PWM OFF → Service实时二次复核 → Relay ON → BAT_U 10s稳定 → RUN；
2. 12组电池参数与V2.7原表一致；
3. MOS从95°C开始降额，105°C/1s停机，95°C/1s恢复；
4. 无BAT_I硬件，电池电流只允许ESTIMATED；
5. 300W的12A、360W和95~104°C绝对降额功率仍是候选，需台架冻结。
