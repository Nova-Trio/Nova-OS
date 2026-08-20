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

// When the kernel gets disk & fs drivers remove this

#define PSF1_MAGIC0 0x36
#define PSF1_MAGIC1 0x04

#define PSF1_MODE512 0x01

typedef struct {
  uint8_t magic[2];
  uint8_t mode;
  uint8_t charsize;
} __attribute__((packed)) PSF1_Header;

typedef struct {
  PSF1_Header *header;
  void *glyph_buffer;
} PSF1_Font;

// END

typedef struct {
  BootFramebuffer framebuffer;
  BootMemoryMap memory_map;
  PSF1_Font font; // read above
  void *rsdp;
  uint64_t *pml4;
  BootRegion kernel;
  BootRegion stack;
} BootInfo;
