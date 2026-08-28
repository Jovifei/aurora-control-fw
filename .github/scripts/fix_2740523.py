from pathlib import Path
import shutil

ROOT = Path(__file__).resolve().parents[2]


def replace_once(path: str, old: str, new: str) -> None:
    file = ROOT / path
    text = file.read_text(encoding="utf-8")
    count = text.count(old)
    if count != 1:
        raise SystemExit(f"{path}: expected one replacement target, found {count}")
    file.write_text(text.replace(old, new, 1), encoding="utf-8", newline="\n")


# charger.c：covered-default 被移除后，补回运行时非法枚举 fail-safe。
charger_marker = "    /* 电池超过档案保护电压时立即进入软件FAULT，不再推进普通状态。 */\n"
charger_guard = """    /*
     * 编译器可以证明正常枚举覆盖完整，但运行时上下文仍可能因RAM破坏得到非法值。
     * 不使用covered-default，而是在进入switch前显式校验并锁入FAULT。
     */
    if ((uint32_t)ctx->state > (uint32_t)AURORA_CHARGE_FAULT)
    {
        enter_state(ctx, AURORA_CHARGE_FAULT, now_ms);
    }

""" + charger_marker
replace_once("app/src/charger.c", charger_marker, charger_guard)

# power_stage.c：非法状态 fail-closed；故障恢复不得绕过继电器最短放能时间。
old_recovery = """    if (!protection_safe && (ctx->state != AURORA_POWER_FAULT))
    {
        /* 软件命令先撤销PWM；Service随后执行硬件关波并延时释放继电器。 */
        enter_state(ctx, AURORA_POWER_FAULT, now_ms);
    }
    else if ((ctx->state == AURORA_POWER_FAULT) && protection_safe)
    {
        /* 即使故障已清除，也禁止直接恢复RUN，必须重新识别电池并完成预充。 */
        enter_state(ctx, AURORA_POWER_WAIT_BATTERY, now_ms);
    }
"""
new_recovery = """    /*
     * switch不再依赖default兜底：若上下文状态越界，立即关闭继电器并锁入FAULT。
     * 这样即使RAM被破坏，也不会沿用故障前的relay/duty状态。
     */
    if ((uint32_t)ctx->state > (uint32_t)AURORA_POWER_FAULT)
    {
        ctx->relay_closed = false;
        enter_state(ctx, AURORA_POWER_FAULT, now_ms);
    }

    if (!protection_safe && (ctx->state != AURORA_POWER_FAULT))
    {
        /* 软件命令先撤销PWM；Service随后执行硬件关波并延时释放继电器。 */
        enter_state(ctx, AURORA_POWER_FAULT, now_ms);
    }
"""
replace_once("app/src/power_stage.c", old_recovery, new_recovery)

old_fault_case = """    case AURORA_POWER_FAULT:
        ctx->duty_q15 = 0U;
        if (elapsed_ms(now_ms, ctx->state_since_ms) >= AURORA_RELAY_FAULT_RELEASE_MS)
        {
            ctx->relay_closed = false;
        }
        break;
"""
new_fault_case = """    case AURORA_POWER_FAULT:
        ctx->duty_q15 = 0U;
        if (elapsed_ms(now_ms, ctx->state_since_ms) >= AURORA_RELAY_FAULT_RELEASE_MS)
        {
            /*
             * 无论故障信号多快恢复，都先满足最短放能时间再断继电器；
             * 只有保护链同时恢复安全后，下一拍才回WAIT_BATTERY重新预充。
             */
            ctx->relay_closed = false;
            if (protection_safe)
            {
                enter_state(ctx, AURORA_POWER_WAIT_BATTERY, now_ms);
            }
        }
        break;
"""
replace_once("app/src/power_stage.c", old_fault_case, new_fault_case)

# C 回归：非法状态和过早 fault rearm。
old_charger_tail = """    CHECK(o.state == AURORA_CHARGE_CC);
    CHECK(o.allow_charge);
}
"""
new_charger_tail = """    CHECK(o.state == AURORA_CHARGE_CC);
    CHECK(o.allow_charge);

    /* 非法枚举状态必须fail-safe到FAULT，不能因为default被移除而静默悬空。 */
    c.state = (aurora_charge_state_t)0x7FU;
    o = aurora_charger_step(&c, &s, false, false, 20U);
    CHECK(o.state == AURORA_CHARGE_FAULT);
    CHECK(!o.allow_charge);
}
"""
replace_once("tests/test_main.c", old_charger_tail, new_charger_tail)

old_rearm = """    /* 清故障后不得直接回RUN，必须回到电池识别/预充。 */
    command = aurora_power_stage_step(&ctx, &sample, &mppt, &charger, true, 200U);
    CHECK(command.state == AURORA_POWER_PRECHARGE);
    CHECK(!command.pwm_enable);
    CHECK(ctx.duty_q15 == 0U);

    command = aurora_power_stage_step(&ctx, &sample, &mppt, &charger, true, 201U);
    CHECK(command.pwm_enable);
    CHECK(command.duty_q15 > 0U);
    CHECK(command.duty_q15 <= AURORA_DUTY_STEP_Q15);
}
"""
new_rearm = """    /* 故障即使很快消失，也必须先满足继电器最短放能时间。 */
    command = aurora_power_stage_step(&ctx, &sample, &mppt, &charger, true, 110U);
    CHECK(command.state == AURORA_POWER_FAULT);
    CHECK(!command.pwm_enable);
    CHECK(command.relay_enable);

    /* 满足放能时间后先回WAIT_BATTERY，下一拍再重新进入预充。 */
    command = aurora_power_stage_step(&ctx, &sample, &mppt, &charger, true, 121U);
    CHECK(command.state == AURORA_POWER_WAIT_BATTERY);
    CHECK(!command.pwm_enable);
    CHECK(!command.relay_enable);
    CHECK(ctx.duty_q15 == 0U);

    command = aurora_power_stage_step(&ctx, &sample, &mppt, &charger, true, 122U);
    CHECK(command.state == AURORA_POWER_PRECHARGE);
    CHECK(!command.pwm_enable);

    command = aurora_power_stage_step(&ctx, &sample, &mppt, &charger, true, 123U);
    CHECK(command.pwm_enable);
    CHECK(command.duty_q15 > 0U);
    CHECK(command.duty_q15 <= AURORA_DUTY_STEP_Q15);

    /* 状态值损坏时必须立刻关继电器、清Duty并进入FAULT。 */
    ctx.state = (aurora_power_state_t)0x7FU;
    ctx.relay_closed = true;
    ctx.duty_q15 = 12000U;
    ctx.power_integral = 2048LL;
    command = aurora_power_stage_step(&ctx, &sample, &mppt, &charger, true, 200U);
    CHECK(command.state == AURORA_POWER_FAULT);
    CHECK(!command.pwm_enable);
    CHECK(!command.relay_enable);
    CHECK(command.duty_q15 == 0U);
    CHECK(ctx.power_integral == 0LL);
}
"""
replace_once("tests/test_main.c", old_rearm, new_rearm)

# Python 契约测试纳入统一质量门。
run_checks_marker = """run([sys.executable, "tools/check_architecture.py"])
run([sys.executable, "tools/check_code_style.py"])

"""
run_checks_new = """run([sys.executable, "tools/check_architecture.py"])
run([sys.executable, "tools/check_code_style.py"])
run([sys.executable, "-m", "unittest", "discover", "-s", "tests",
     "-p", "test_*.py", "-v"])

"""
replace_once("tools/run_checks.py", run_checks_marker, run_checks_new)

# 防止 Python 缓存再次进入仓库。
gitignore = ROOT / ".gitignore"
text = gitignore.read_text(encoding="utf-8")
if "__pycache__/" not in text:
    text = text.rstrip() + "\n\n# Python caches\n__pycache__/\n*.py[cod]\n"
    gitignore.write_text(text, encoding="utf-8", newline="\n")
for cache in ROOT.rglob("__pycache__"):
    if cache.is_dir():
        shutil.rmtree(cache)

# 一次性 v0.7.1 Python 迁移脚本已过期，避免被旧 workflow 手动误触。
stale_script = ROOT / ".github/scripts/refactor_v071.py"
if stale_script.exists():
    stale_script.unlink()

# 审计文档。
audit = ROOT / "docs/19-编译修复提交2740523审计.md"
audit.write_text("""# 19 · 编译修复提交 2740523 审计

## 结论

提交 `2740523a441eec4d3b61dfbf933fafa5c9d8764d` 的主要目的为解决 ARM Compiler 6 编译告警/报错。审计确认大部分结构体重排、显式保留字、`noreturn` 与协议解析状态保护不改变正常业务语义，但发现并修复以下问题：

1. `charger.c` 与 `power_stage.c` 为消除 covered-default 类告警移除了 `default`，却没有同步增加运行时非法枚举检查；RAM/上下文损坏后可能留下未知状态。
2. `power_stage.c` 在 `FAULT` 且 `protection_safe` 很快恢复时会在函数顶部立即跳回 `WAIT_BATTERY`，从而绕过 `AURORA_RELAY_FAULT_RELEASE_MS` 的最短放能时间。
3. `tests/__pycache__/*.pyc` 被误提交，且 `.gitignore` 未屏蔽 Python 缓存。
4. 新增的 Python 架构测试没有被 `tools/run_checks.py` 调用，质量门实际漏跑一类测试。
5. 旧 v0.7.1 一次性迁移脚本已不应继续参与当前工程维护。

本次增加显式状态范围检查；FAULT 恢复改为先满足继电器放能时间，再断开继电器并回 `WAIT_BATTERY` 重新预充；同时补充 C 回归测试、Python 测试入口和缓存规则。

## 本次未回退的编译修复

该提交还修改了 `vendor/ddl` 中 ATMR 结构体布局和 Flash 写入实现。当前调用与本仓库头/源文件一致；Flash 写入改为按字节组装 32 位 word，反而避免了原实现潜在的未对齐读取，因此本轮不回退。

长期建议仍是：保持官方 Vendor SDK 原样，通过产品封装层或单独告警策略处理第三方库 warning，而不是持续修改厂商源码。待 Keil AC6 构建环境稳定后再专项清理。

## 仍需目标板验证

- COMP0/COMP2 极性、COMP0_OUT→U6 EN 和 ATMR Break 真实链路；
- ADC 比例、OPA 零点/方向和定时触发采样相位；
- CCR preload 仅在自然 UPDATE 生效、首脉冲和故障后不重发波；
- Flash 512 B 擦除页、A/B Journal 掉电原子性；
- IWDT 实际超时时间；
- 300 W 低压限流到额定功率的温升与 SOA。

当前 `BOARD_POWER_OUTPUT_ALLOWED` 继续保持 `0`。
""", encoding="utf-8", newline="\n")

index = ROOT / "docs/00-文档索引.md"
index_text = index.read_text(encoding="utf-8").rstrip() + "\n"
if "19-编译修复提交2740523审计" not in index_text:
    index_text += "- `19-编译修复提交2740523审计.md`：审计编译修复提交的语义变化、回归缺口和安全修复。\n"
    index.write_text(index_text, encoding="utf-8", newline="\n")
