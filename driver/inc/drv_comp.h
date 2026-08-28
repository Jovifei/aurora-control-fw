#ifndef AURORA_DRV_COMP_H
#define AURORA_DRV_COMP_H

#include <stdbool.h>
#include <stdint.h>

/* 驱动故障位：MOS支路快速过流。 */
#define DRV_FAULT_MOS_OCP                           (1UL << 0)
/* 驱动故障位：PV输入快速过流。 */
#define DRV_FAULT_PV_OCP                            (1UL << 1)

bool drv_comp_init(void);
uint32_t drv_comp_fault_mask(void);
void drv_comp_irq_ack(void);

#endif
