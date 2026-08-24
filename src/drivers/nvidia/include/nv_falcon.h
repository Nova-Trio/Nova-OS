#pragma once

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <novamod.h>
#include <nv_device.h>

// base addresses
#define NV_FALCON_GSP_BASE 0x00110000U
#define NV_FALCON_GSP_FBIF_BASE 0x00110600U
#define NV_FALCON_GSP_RISCV_BASE 0x00111000U

#define NV_FALCON_SEC2_BASE 0x00840000U
#define NV_FALCON_SEC2_FBIF_BASE 0x00840600U

#define NV_FALCON_PMU_BASE 0x0010A000U
#define NV_FALCON_PMU_FBIF_BASE 0x0010A600U

// reg offsets from base
#define NV_FALCON_IRQSCLR 0x00000004U
#define NV_FALCON_IRQSCLR_HALT_SET (1U << 4)
#define NV_FALCON_IRQSCLR_SWGEN0_SET (1U << 6)

#define NV_FALCON_IRQSTAT 0x00000008U
#define NV_FALCON_IRQSTAT_HALT_TRUE (1U << 4)
#define NV_FALCON_IRQSTAT_SWGEN0_TRUE (1U << 6)

#define NV_FALCON_IRQMODE 0x0000000CU
#define NV_FALCON_IRQMSET 0x00000010U
#define NV_FALCON_IRQMCLR 0x00000014U
#define NV_FALCON_IRQMASK 0x00000018U
#define NV_FALCON_IRQDEST 0x0000001CU

#define NV_FALCON_MAILBOX0 0x00000040U
#define NV_FALCON_MAILBOX1 0x00000044U
#define NV_FALCON_RM 0x00000084U
#define NV_FALCON_OS 0x00000080U
#define NV_FALCON_DEBUGINFO 0x00000094U

#define NV_FALCON_HWCFG2 0x000000F4U
#define NV_FALCON_HWCFG2_RISCV_ENABLE (1U << 10)

#define NV_FALCON_CPUCTL 0x00000100U
#define NV_FALCON_CPUCTL_STARTCPU (1U << 1)
#define NV_FALCON_CPUCTL_HALTED (1U << 4)
#define NV_FALCON_CPUCTL_ALIAS_EN (1U << 6)

#define NV_FALCON_BOOTVEC 0x00000104U
#define NV_FALCON_HWCFG 0x00000108U
#define NV_FALCON_HWCFG_IMEM_SIZE_MASK 0x000001FFU

#define NV_FALCON_DMACTL 0x0000010CU
#define NV_FALCON_DMACTL_REQUIRE_CTX (1U << 0)
#define NV_FALCON_DMACTL_DMEM_SCRUBBING (1U << 1)
#define NV_FALCON_DMACTL_IMEM_SCRUBBING (1U << 2)

#define NV_FALCON_IMEMC(i) (0x00000180U + ((i) * 16U))
#define NV_FALCON_IMEMC_OFFS_SHIFT 2U
#define NV_FALCON_IMEMC_OFFS_MASK 0x000000FCU
#define NV_FALCON_IMEMC_BLK_SHIFT 8U
#define NV_FALCON_IMEMC_BLK_MASK 0x0000FF00U
#define NV_FALCON_IMEMC_AINCW (1U << 24)
#define NV_FALCON_IMEMC_SECURE (1U << 28)

#define NV_FALCON_IMEMD(i) (0x00000184U + ((i) * 16U))
#define NV_FALCON_IMEMT(i) (0x00000188U + ((i) * 16U))
#define NV_FALCON_IMEMT_TAG_SHIFT 0U
#define NV_FALCON_IMEMT_TAG_MASK 0x0000FFFFU

#define NV_FALCON_DMEMC(i) (0x000001C0U + ((i) * 8U))
#define NV_FALCON_DMEMC_OFFS_SHIFT 2U
#define NV_FALCON_DMEMC_OFFS_MASK 0x000000FCU
#define NV_FALCON_DMEMC_BLK_SHIFT 8U
#define NV_FALCON_DMEMC_BLK_MASK 0x0000FF00U
#define NV_FALCON_DMEMC_AINCW (1U << 24)
#define NV_FALCON_DMEMC_AINCR (1U << 25)

#define NV_FALCON_DMEMD(i) (0x000001C4U + ((i) * 8U))

#define NV_FALCON_ENGINE_RESET 0x000003C0U
#define NV_FALCON_ENGINE_RESET_TRUE 0x00000001U
#define NV_FALCON_ENGINE_RESET_FALSE 0x00000000U

// DMA
#define FALCON_DMAIDX_UCODE 0U
#define FALCON_DMAIDX_VIRT_SYS_COH 1U
#define FALCON_DMAIDX_VIRT_SYS_NCOH 2U
#define FALCON_DMAIDX_PHYS_VID 3U
#define FALCON_DMAIDX_PHYS_SYS_COH 4U
#define FALCON_DMAIDX_PHYS_SYS_NCOH 5U

// gsp only register offsets (from riscv base)
#define NV_PRISCV_RISCV_CORE_SWITCH_STATUS 0x00000240U
#define NV_PRISCV_RISCV_CORE_SWITCH_ACTIVE (1U << 0)

#define NV_PRISCV_RISCV_CPUCTL 0x00000268U
#define NV_PRISCV_RISCV_CPUCTL_STARTCPU (1U << 0)
#define NV_PRISCV_RISCV_CPUCTL_RESET (1U << 1)

#define NV_PRISCV_RISCV_BCR_CTRL 0x00000200U
#define NV_PRISCV_RISCV_IRQMASK 0x000002B4U

// FBIF regs
#define NV_FALCON_FBIF_TRANSCFG(i) (0x00000000U + ((i) * 4U))
#define NV_FALCON_FBIF_TRANSCFG_TARGET_LOCAL_FB 0x00000000U
#define NV_FALCON_FBIF_TRANSCFG_TARGET_COHERENT_SYSMEM 0x00000001U
#define NV_FALCON_FBIF_TRANSCFG_TARGET_NONCOH_SYSMEM 0x00000002U
#define NV_FALCON_FBIF_TRANSCFG_MEM_TYPE_PHYSICAL 0x00000004U

#define NV_FALCON_FBIF_CTL 0x00000024U
#define NV_FALCON_FBIF_CTL_ALLOW_PHYS_NO_CTX (1U << 7)

// misc
#define NV_FALCON_IMEM_BLK_SIZE 256U
#define NV_FALCON_DMEM_ALIGN_SIZE 4U
#define NV_FALCON_RESET_PROPAGATION_CYCLES 16U
#define NV_FALCON_DEFAULT_TIMEOUT_US 1000000U // 1sec

typedef struct NvFalcon {
  uint32_t base_addr;
  uint32_t fbif_base;
  uint32_t riscv_base;
  bool is_riscv;
  const char *name;
} NvFalcon;


void nv_falcon_init(NvFalcon *falcon, uint32_t base_addr, uint32_t fbif_base, uint32_t riscv_base, bool is_riscv, const char *name);
void nv_falcon_init_gsp(NvFalcon *falcon);
void nv_falcon_init_sec2(NvFalcon *falcon);

int nv_falcon_reset(const NvDevice *dev, const NvFalcon *falcon);
int nv_falcon_wait_mem_scrubbing(const NvDevice *dev, const NvFalcon *falcon);

int nv_falcon_imem_write(const NvDevice *dev, const NvFalcon *falcon, uint32_t target_imem_addr, const uint8_t *src, size_t size, bool secure, uint32_t tag);
int nv_falcon_dmem_write(const NvDevice *dev, const NvFalcon *falcon, uint32_t target_dmem_addr, const uint8_t *src, size_t size);
int nv_falcon_dmem_read(const NvDevice *dev, const NvFalcon *falcon, uint32_t target_dmem_addr, uint8_t *dst, size_t size);

int nv_falcon_setup_fbif_aperture(const NvDevice *dev, const NvFalcon *falcon, uint32_t dma_idx, uint32_t target, uint32_t mem_type);

void nv_falcon_set_bootvec(const NvDevice *dev, const NvFalcon *falcon, uint32_t bootvec);
void nv_falcon_start_cpu(const NvDevice *dev, const NvFalcon *falcon);
int nv_falcon_wait_halt(const NvDevice *dev, const NvFalcon *falcon, uint32_t timeout_us);
void nv_falcon_clear_intr(const NvDevice *dev, const NvFalcon *falcon, uint32_t mask);

void nv_falcon_mailbox_write(const NvDevice *dev, const NvFalcon *falcon, uint8_t mbox_idx, uint32_t val);
uint32_t nv_falcon_mailbox_read(const NvDevice *dev, const NvFalcon *falcon, uint8_t mbox_idx);
void nv_falcon_set_os(const NvDevice *dev, const NvFalcon *falcon, uint32_t os_version);

bool nv_falcon_is_riscv_active(const NvDevice *dev, const NvFalcon *falcon);
int nv_falcon_reset_into_riscv(const NvDevice *dev, const NvFalcon *falcon);
