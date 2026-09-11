#pragma once
#include <novamod.h>
#include <stdint.h>

typedef struct {
  uint64_t physAddr;
  uint64_t virtAddr;
  uint64_t size;
} NvBar;

typedef enum NvGpuChipset {
  NV_TU100 = 0x160,
  NV_TU102 = 0x162,
  NV_TU104 = 0x164,
  NV_TU106 = 0x166,
  NV_TU117 = 0x167,
  NV_TU116 = 0x168
} NvGpuChipset;

typedef struct NvDevice {
  const PciDevice* dev;
  NvBar bar0;
  NvBar bar1;
  NvBar bar3;

  uint32_t chipset;
  uint32_t chiprev;
  NvGpuChipset gpuArch;

  uint32_t crystal;

  struct NvDevice* next;
} NvDevice;

static inline uint32_t nvRd32(const NvDevice* dev, uint32_t reg){
  return *(volatile uint32_t*)((uint8_t*)dev->bar0.virtAddr + reg);
}


static inline void nv_wr32(const NvDevice *dev, uint32_t reg, uint32_t val) {
  *(volatile uint32_t*)((uint8_t*)dev->bar0.virtAddr + reg) = val;
}

