#pragma once
#include <novamod.h>
#include <nv_vmm.h>

typedef enum {
  NV_ARCH_UNKNOWN = 0,
  NV_ARCH_TURING,
} NvArchType;

typedef struct {
  uint32_t raw_boot0; // 32bit val read from NV_PMC_BOOT_0
  uint16_t chipset; // Combined chipset identifier, (arch << 4) | impl
  uint8_t major_rev; // bits [7:4] of raw_boot0
  uint8_t minor_rev; // bits [3:0] of raw_boot0
  NvArchType arch;
  const char *chip_name; // Human readable string for the chip (incl. arch)
  const char *arch_name; // Human readable string for the generation
} NvChipInfo;

typedef struct {
  uint64_t phys_addr;
  uint64_t virt_addr;
  uint64_t size;
} NvBar;

struct NvDevice;

// For later, supporting multiple architectures
typedef struct {
  int (*init)(struct NvDevice *dev);
  void (*cleanup)(struct NvDevice *dev);
} NvArchOps;

// Defines a single physical GPU
typedef struct NvDevice {
  const PciDevice *pci_dev;
  NvBar bar0;
  NvBar bar1;
  NvBar bar3;
  NvChipInfo chip;
  NvVmm vmm;
  NvDmaBuffer bar1_inst;
  const NvArchOps *ops;
  struct NvDevice *next;
} NvDevice;

static inline uint32_t nv_rd32(const NvDevice *dev, uint32_t reg) {
  return *(volatile uint32_t *)((uint8_t *)dev->bar0.virt_addr + reg);
}

static inline void nv_wr32(const NvDevice *dev, uint32_t reg, uint32_t val) {
  *(volatile uint32_t *)((uint8_t *)dev->bar0.virt_addr + reg) = val;
}

int nv_device_probe(void);
void nv_device_remove_all(void);
