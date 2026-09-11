#pragma once
#include <novamod.h>
#include <stddef.h>
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

typedef struct NvGspMem {
  size_t size;
  void* data;
  uint64_t addr;
} NvGspMem;

typedef struct NvVbios {
  uint8_t* data;
  size_t size;
  uint32_t bitOffset;
} NvVbios;

typedef struct NvDevice {
  const PciDevice* dev;
  NvBar bar0;
  NvBar bar1;
  NvBar bar3;

  uint32_t chipset;
  uint32_t chiprev;
  NvGpuChipset gpuArch;

  uint32_t crystal;
  NvVbios bios;

  struct NvDevice* next;
} NvDevice;

int nvGspMemAlloc(NvDevice *dev, size_t size, NvGspMem *mem);
void nvGspMemFree(NvDevice *dev, NvGspMem *mem);
int nvVbiosInit(NvDevice *dev);
void nvVbiosFree(NvDevice *dev);

static inline uint32_t nvRd32(const NvDevice* dev, uint32_t reg){
  return *(volatile uint32_t*)((uint8_t*)dev->bar0.virtAddr + reg);
}


static inline void nvWr32(const NvDevice *dev, uint32_t reg, uint32_t val) {
  *(volatile uint32_t*)((uint8_t*)dev->bar0.virtAddr + reg) = val;
}

static inline uint8_t nvRd08(const NvDevice *dev, uint32_t reg) {
  return *(volatile uint8_t *)((uint8_t *)dev->bar0.virtAddr + reg);
}

static inline void nvWr08(const NvDevice *dev, uint32_t reg, uint8_t val) {
  *(volatile uint8_t *)((uint8_t *)dev->bar0.virtAddr + reg) = val;
}

static inline uint16_t nvRd16(const NvDevice *dev, uint32_t reg) {
  return *(volatile uint16_t *)((uint8_t *)dev->bar0.virtAddr + reg);
}

static inline void nvWr16(const NvDevice *dev, uint32_t reg, uint16_t val) {
  *(volatile uint16_t *)((uint8_t *)dev->bar0.virtAddr + reg) = val;
}

static inline uint32_t nvMask32(const NvDevice *dev, uint32_t reg, uint32_t mask, uint32_t val) {
  uint32_t old = nvRd32(dev, reg);
  nvWr32(dev, reg, (old & ~mask) | val);
  return old;
}
