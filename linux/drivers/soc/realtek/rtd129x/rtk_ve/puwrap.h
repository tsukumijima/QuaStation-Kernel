

#ifndef __PU_DRV_H__
#define __PU_DRV_H__

#include <linux/fs.h>
#include <linux/types.h>
#include <linux/reset-helper.h>
#include <linux/reset.h>


#define PDI_IOCTL_MAGIC  'V'

#define PLL_CLK_715 (0x10D336)
#define PLL_CLK_702 (0x10C186)
#define PLL_CLK_675 (0x10C176)
#define PLL_CLK_648 (0x10C166)
#define PLL_CLK_594 (0x10C146)
#define PLL_CLK_567 (0x10C136)
#define PLL_CLK_337 (0x12C176)
#define PLL_CLK_297 (0x12C146)

#define UNUSED_PARAMETER(p) (void)(p)

typedef struct pu_drv_context_t {
    struct fasync_struct *async_queue;
    unsigned long jpu_interrupt_reason;
    unsigned long vpu_interrupt_reason;
} pu_drv_context_t;

#ifdef CONFIG_RTK_RESERVE_MEMORY
int pu_alloc_dma_buffer(unsigned int size, unsigned long *phys_addr, unsigned long *base);
void pu_free_dma_buffer(unsigned long base, unsigned long phys_addr);
int pu_mmap_dma_buffer(struct vm_area_struct *vm);
#endif
void pu_pll_setting(unsigned long offset, unsigned int value, unsigned int bOverwrite, unsigned int bEnable);

#endif
