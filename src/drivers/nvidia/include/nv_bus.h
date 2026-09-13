#pragma once
#include <stdint.h>
#include <nv_device.h>
#include <nv_vmm.h>

// Scratch storage for anything with PMU or GSP, i depending on chip
#define NV_PBUS_SW_SCRATCH(i) (0x00001400 + (i) * 4)

#define NV_PRAMIN_BAR1_BLOCK 0x00B80F40
#define NV_PRAMIN_BAR1_STATUS 0x00B80F50
#define NV_PRAMIN_BAR1_STATUS_BUSY 0x00000003U
#define NV_PRAMIN_BAR1_BLOCK_ENABLE 0x80000000U

/* BAR1 aperture ctrl registers
 * [31]    : Mode (0 = Phys 1 = Virt)
 * [29:28] : Target aperture
 * [27:0]  : 4K page frame number (phys_addr >> 12)
 */
#define NV_PBUS_BAR1_BLOCK 0x00001704
#define NV_PBUS_BAR1_BLOCK_PTR_MASK 0x0FFFFFFFU // Upper bits of the address
#define NV_PBUS_BAR1_BLOCK_TARGET_MASK 0x30000000U
#define NV_PBUS_BAR1_BLOCK_TARGET_SHIFT 28
#define NV_PBUS_BAR1_BLOCK_TARGET_VID_MEM 0x0U // Point into local VRAM
#define NV_PBUS_BAR1_BLOCK_TARGET_SYS_MEM_COHERENT 0x2U // System RAM with coherency
#define NV_PBUS_BAR1_BLOCK_TARGET_SYS_MEM_NONCOHERENT 0x3U // System RAM
#define NV_PBUS_BAR1_BLOCK_MODE_MASK 0x80000000U
#define NV_PBUS_BAR1_BLOCK_MODE_SHIFT 31
#define NV_PBUS_BAR1_BLOCK_MODE_PHYSICAL 0x0U // Use physical address
#define NV_PBUS_BAR1_BLOCK_MODE_VIRTUAL 0x1U // Use GPU virtual address
#define NV_PBUS_BAR1_BLOCK_PTR_SHIFT 0
#define NV_PBUS_BAR1_BLOCK_PTR_ALIGN_SHIFT 12

/*
  Exactly same as BAR1 just this is BAR2 aperture
*/

#define NV_PBUS_BAR2_BLOCK 0x00001714
#define NV_PBUS_BAR2_BLOCK_PTR_MASK 0x0FFFFFFFU
#define NV_PBUS_BAR2_BLOCK_TARGET_MASK 0x30000000U
#define NV_PBUS_BAR2_BLOCK_TARGET_SHIFT 28
#define NV_PBUS_BAR2_BLOCK_TARGET_VID_MEM 0x0U
#define NV_PBUS_BAR2_BLOCK_TARGET_SYS_MEM_COHERENT 0x2U
#define NV_PBUS_BAR2_BLOCK_TARGET_SYS_MEM_NONCOHERENT 0x3U
#define NV_PBUS_BAR2_BLOCK_MODE_MASK 0x80000000U
#define NV_PBUS_BAR2_BLOCK_MODE_SHIFT 31
#define NV_PBUS_BAR2_BLOCK_MODE_PHYSICAL 0x0U
#define NV_PBUS_BAR2_BLOCK_MODE_VIRTUAL 0x1U
#define NV_PBUS_BAR2_BLOCK_PTR_SHIFT 0
#define NV_PBUS_BAR2_BLOCK_PTR_ALIGN_SHIFT 12

int nv_bus_bind_bar1_vmm(NvDevice *dev, const NvVmm *vmm);
int nv_bus_bind_bar1_phys(const NvDevice *dev, uint64_t phys_addr, uint32_t target);
