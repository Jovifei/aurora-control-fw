# G00 — Flash 分区冻结

- **Gate**: G0-3
- **状态**: IN_PROGRESS（地址已定，待 Keil 链接实测确认）
- **器件**: G32F031K8T6，64 KB Flash（`0x0000_0000`~`0x0000_FFFF`），物理擦除页 **512 B**

---

## 1. 冲突根因

| 来源 | 声明 | 冲突 |
|---|---|---|
| 目标工程 `bsp_ota.c:4` | `#define OTA_FLAG_ADDRESS 0x0000FE00U`（与 Bootloader 约定的 OTA 标志扇区，magic `0x4F544131` = ASCII `"OTA1"`） | — |
| aurora `board_config.h:151` | `#define BOARD_FLASH_PAGE_B_ADDRESS (0x0000FE00UL)` | **与 OTA 标志页完全重叠** |

若照搬 aurora 地址，参数 Journal 的 B 页写入会覆盖 OTA 标志页，最坏情况：一次参数保存后设备下次上电被 Bootloader 误判为 OTA 请求，卡在 Bootloader。

---

## 2. 冻结后的 Flash 映射

| 区段 | 起始 | 结束 | 大小 | 用途 |
|---|---|---|---|---|
| Bootloader | `0x0000_0000` | `0x0000_0FFF` | 4 KB | 独立工程，本次移植不触碰 |
| **Application** | `0x0000_1000` | `0x0000_F9FF` | **`0xEA00` = 59 904 B** | 本工程代码 + 常量 |
| **参数页 A** | `0x0000_FA00` | `0x0000_FBFF` | 512 B | 双页 Journal A（G9 才启用） |
| **参数页 B** | `0x0000_FC00` | `0x0000_FDFF` | 512 B | 双页 Journal B（G9 才启用） |
| OTA 标志页 | `0x0000_FE00` | `0x0000_FFFF` | 512 B | 与 Bootloader 约定，**不得被任何其他代码写入** |

校验：`0xFA00 - 0x1000 = 0xEA00` ✅；`0xFA00 + 0x200 = 0xFC00` ✅；`0xFC00 + 0x200 = 0xFE00` ✅（紧邻 OTA 页且不重叠）。

---

## 3. 需同步修改的位置

| 文件 | 原值 | 新值 |
|---|---|---|
| `Application/driver/inc/board_config.h` | `BOARD_FLASH_PAGE_A_ADDRESS (0x0000FC00UL)` | `(0x0000FA00UL)` |
| `Application/driver/inc/board_config.h` | `BOARD_FLASH_PAGE_B_ADDRESS (0x0000FE00UL)` | `(0x0000FC00UL)` |
| `Application/Project/MDK/IAP_Application.uvprojx` | IROM1 `StartAddress 0x1000` / `Size 0xEE00` | `Size 0xEA00` |
| `Application/driver/src/bsp_ota.c` | `OTA_FLAG_ADDRESS 0x0000FE00U` | **不改**（Bootloader 约定） |

`VECT_TAB_OFFSET 0x1000` 与 `SCB->VTOR = FLASH_BASE | VECT_TAB_OFFSET`（`app/src/system_g32f031.c`）保持不变。

---

## 4. 为什么现在就收窄 IROM1

G1 尚未移植 `drv_flash.c`，参数页此刻无人写入。但**先收窄 IROM1** 可以让链接器从此刻起就不可能把代码/常量放进参数页区间 —— 这是零成本的静态保护。若等到 G9 才收窄，中间任何一次代码增长都可能悄悄占用 `0xFA00` 之后的空间，届时收窄会突然链接失败且难以定位。

当前 Application 代码量远小于 59 904 B，收窄不会导致溢出。

---

## 5. 验收判据

- [ ] Keil Rebuild 后 `.map` 中最高地址的 RO/RW 段结束地址 **< `0x0000FA00`**
- [ ] `.map` 中不存在任何落在 `0xFA00`~`0xFFFF` 的输出段
- [ ] `bsp_ota.c` 的 `OTA_FLAG_ADDRESS` 仍为 `0x0000FE00`，未被改动
