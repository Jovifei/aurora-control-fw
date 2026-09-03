# Keil ARM Compiler 6 工程

v0.8.3 后 Keil 工程直接位于 `project/`，不再额外套 `project/keil/`。

## 文件

```text
project/
├─ AuroraControl.uvprojx
├─ AuroraControl.sct
└─ README.md
```

打开 `AuroraControl.uvprojx` 后使用 Geehy G32F031 DFP 和 ARM Compiler 6 构建。

## 两层源码关系

```text
app/src/*.c
    ↓ 只调用
app/inc/*.h + driver/inc/*.h
    ↓
driver/src/*.c
    ↓
vendor CMSIS / Device / DDL
```

`app/src/main.c`包含系统入口、应用调度、旧Service职责和业务组合；`app/src/interrupts.c`只做Driver应答、快速关波和事件投递。APP不得直接包含G32/DDL寄存器头。

## 当前安全状态

`BOARD_POWER_OUTPUT_ALLOWED`继续保持0。Host/Clang语法检查通过不能替代Keil AC6真实链接、MAP检查、弱光PVD启动波形、继电器预充和300W功率台架。

## AC6优化级别

产品目标固定使用 **Optimization Level 1 (`-O1`)**。G32F031启动文件栈为1024B；二次目标栈审计中，Clang Cortex-M0+ O0按当前中断嵌套预算投影为1776B，明确超过1KB；O1为824B、O2为828B（均已含128B额外保守裕量）。因此O0只允许临时诊断，不是可发布构建配置。永久质量门同时验证O1与O2，最终Keil MAP和实板栈水位仍为发布证据。
