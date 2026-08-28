#ifndef AURORA_DRV_COMP_H
#define AURORA_DRV_COMP_H

#include "driver.h"

#ifdef __cplusplus
extern "C" {
#endif

/* 初始化内部OPA/COMP快速故障链。 */
bool drv_comp_init(void);
/* 读取已锁存的比较器故障原因。 */
uint32_t drv_comp_fault_mask(void);
/* 应答比较器中断标志，不清除安全Break锁存。 */
void drv_comp_irq_ack(void);

#ifdef __cplusplus
}
#endif

#endif
