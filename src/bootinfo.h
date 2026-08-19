#pragma once
#include <stdint.h>

#define HHDM_BASE 0xFFFF800000000000ULL

typedef enum {
  BOOT_PIXEL_RGB = 0,
  BOOT_PIXEL_BGR = 1,
  BOOT_PIXEL_BITMASK = 2,
  BOOT_PIXEL_BLT = 3
} BootPixelFormat;

typedef struct {
  uint64_t base;
  uint64_t size;
  uint32_t width;
  uint32_t height;
  uint32_t pixels_per_scanline;
  BootPixelFormat format;
} BootFramebuffer;

typedef struct {
  void *map;
  uint64_t size;
  uint64_t descriptor_size;
  uint32_t descriptor_version;
} BootMemoryMap;

typedef struct {
  uint64_t phys_base;
  uint64_t virt_base;
  uint64_t size;
} BootRegion;

typedef struct {
  BootFramebuffer framebuffer;
  BootMemoryMap memory_map;
  void *rsdp;
  uint64_t *pml4;
  BootRegion kernel;
  BootRegion stack;
} BootInfo;
