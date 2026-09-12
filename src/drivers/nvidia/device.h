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

typedef struct NvBitEntry {
  uint8_t id;
  uint8_t version;
  uint16_t length;
  uint16_t offset;
} NvBitEntry;

typedef struct NvPmuEntry {
  uint8_t type;
  uint32_t data;
} NvPmuEntry;

typedef struct NvFalcon {
  struct NvDevice *dev;
  const char *name;
  uint32_t addr;
  uint32_t addr2;
  uint32_t codeLimit;
  uint32_t dataLimit;
  uint32_t version;
  uint8_t secret;
} NvFalcon;

typedef struct NvFalconBootloader {
  void *raw;
  size_t rawSize;
  const uint8_t *code;
  uint32_t codeSize;
  uint32_t bootAddr;
} NvFalconBootloader;

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
  NvFalcon gspFalcon;
  NvFalcon sec2Falcon;
  NvFalconBootloader bl;

  struct NvDevice* next;
} NvDevice;

int nvGspMemAlloc(NvDevice *dev, size_t size, NvGspMem *mem);
void nvGspMemFree(NvDevice *dev, NvGspMem *mem);
int nvVbiosInit(NvDevice *dev);
void nvVbiosFree(NvDevice *dev);
uint8_t nvbiosRd08(const NvDevice *dev, uint32_t addr);
uint16_t nvbiosRd16(const NvDevice *dev, uint32_t addr);
uint32_t nvbiosRd32(const NvDevice *dev, uint32_t addr);
void *nvbiosPointer(const NvDevice *dev, uint32_t addr);

int nvbiosBitEntry(const NvDevice *dev, uint8_t id, NvBitEntry *bit);
uint32_t nvbiosPmuTe(const NvDevice *dev, uint8_t *ver, uint8_t *hdr, uint8_t *cnt, uint8_t *len);
uint32_t nvbiosPmuEp(const NvDevice *dev, int idx, uint8_t *ver, uint8_t *hdr, NvPmuEntry *info);
int nvVbiosFindFwsec(const NvDevice *dev, NvPmuEntry *info);

int nvFalconInit(NvFalcon *flcn, NvDevice *dev, const char *name, uint32_t addr);
int nvFalconReset(NvFalcon *flcn);
void nvFalconImemPioWr(NvFalcon *flcn, const void *src, uint32_t imemAddr, uint32_t len, int sec);
void nvFalconDmemPioWr(NvFalcon *flcn, const void *src, uint32_t dmemAddr, uint32_t len);
void nvFalconDmemPioRd(NvFalcon *flcn, void *dst, uint32_t dmemAddr, uint32_t len);
int nvFalconRiscvActive(NvFalcon *flcn);
int nvFalconLoadBl(NvFalconBootloader *bl, const char *path);
void nvFalconFreeBl(NvFalconBootloader *bl);

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
