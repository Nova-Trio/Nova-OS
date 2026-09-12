#include "device.h"
#include <novamod.h>
#include <stdint.h>

#define FALCON_REG_INTR_TRIGGER 0x004
#define FALCON_REG_INTR_STATUS 0x008
#define FALCON_REG_MAILBOX0 0x040
#define FALCON_REG_MAILBOX1 0x044
#define FALCON_REG_CONTROL 0x100
#define FALCON_REG_BOOT_ADDR 0x104
#define FALCON_REG_LIMITS 0x108
#define FALCON_REG_MEM_SCRUB 0x10c
#define FALCON_REG_CAPS 0x12c
#define FALCON_REG_RESET_ENG 0x3c0

static inline uint32_t nvFalconRd32(const NvFalcon *flcn, uint32_t reg) {
  return nvRd32(flcn->dev, flcn->addr + reg);
}

static inline void nvFalconWr32(const NvFalcon *flcn, uint32_t reg, uint32_t val) {
  nvWr32(flcn->dev, flcn->addr + reg, val);
}

static inline uint32_t nvFalconMask32(const NvFalcon *flcn, uint32_t reg, uint32_t mask, uint32_t val) {
  return nvMask32(flcn->dev, flcn->addr + reg, mask, val);
}

int nvFalconInit(NvFalcon* flcn, NvDevice* dev, const char* name, uint32_t addr){
  flcn->dev = dev;
  flcn->name = name;
  flcn->addr = addr;
  flcn->addr2 = 0x1000;

  uint32_t caps = nvFalconRd32(flcn, FALCON_REG_CAPS);
  flcn->version = caps & 0x0F;
  flcn->secret = (caps >> 4) & 0x03;

  uint32_t limits = nvFalconRd32(flcn, FALCON_REG_LIMITS);
  flcn->codeLimit = (limits & 0x1FF) << 8;
  flcn->dataLimit = (limits & 0x3FE00) >> 1;

  if(flcn->codeLimit == 0){
    kprintf("Falcon %s returned code limit of 0\n", name);
    flcn->codeLimit = 0x10000;
  }

  return 0;
}

int nvFalconReset(NvFalcon* flcn){
  nvFalconMask32(flcn, 0x048, 0x00000003, 0x00000000);
  nvFalconWr32(flcn, 0x014, 0xFFFFFFFF);

  nvFalconMask32(flcn, FALCON_REG_RESET_ENG, 0x00000001, 0x00000001);
  hpet_sleep_us(10);
  nvFalconMask32(flcn, FALCON_REG_RESET_ENG, 0x00000001, 0x00000000);

  uint64_t start = hpet_get_millis();
  while ((nvFalconRd32(flcn, FALCON_REG_MEM_SCRUB) & 0x00000006) != 0) {
    if (hpet_get_millis() - start > 100) {
      kprintf("[NV] %s: Timeout waiting for memory scrubbing\n", flcn->name);
      return -1;
    }
    hpet_sleep_us(10);
  }

  nvFalconWr32(flcn, 0x084, nvRd32(flcn->dev, 0x000000));
  return 0;
}

void nvFalconImemPioWr(NvFalcon *flcn, const void *src, uint32_t imemAddr, uint32_t len, int sec) {
  uint32_t cmd = (sec ? (1U << 28) : 0) | (1U << 24) | imemAddr;
  nvFalconWr32(flcn, 0x180, cmd);
  nvFalconWr32(flcn, 0x188, imemAddr >> 8);

  const uint8_t *p = (const uint8_t *)src;
  for (uint32_t i = 0; i < len; i += 4) {
    uint32_t val = 0;
    size_t copyLen = (len - i < 4) ? (len - i) : 4;
    memcpy(&val, p + i, copyLen);
    nvFalconWr32(flcn, 0x184, val);
  }
}

void nvFalconDmemPioWr(NvFalcon *flcn, const void *src, uint32_t dmemAddr, uint32_t len) {
  nvFalconWr32(flcn, 0x1c0, (1U << 24) | dmemAddr);

  const uint8_t *p = (const uint8_t *)src;
  for (uint32_t i = 0; i < len; i += 4) {
    uint32_t val = 0;
    size_t copyLen = (len - i < 4) ? (len - i) : 4;
    memcpy(&val, p + i, copyLen);
    nvFalconWr32(flcn, 0x1c4, val);
  }
}

void nvFalconDmemPioRd(NvFalcon *flcn, void *dst, uint32_t dmemAddr, uint32_t len) {
  nvFalconWr32(flcn, 0x1c0, (1U << 25) | dmemAddr);

  uint8_t *p = (uint8_t *)dst;
  for (uint32_t i = 0; i < len; i += 4) {
    uint32_t val = nvFalconRd32(flcn, 0x1c4);
    size_t copyLen = (len - i < 4) ? (len - i) : 4;
    memcpy(p + i, &val, copyLen);
  }
}

int nvFalconRiscvActive(NvFalcon *flcn) {
  return (nvFalconRd32(flcn, flcn->addr2 + 0x240) & 0x00000001) != 0;
}

struct nvfw_bin_hdr {
  uint32_t bin_magic;
  uint32_t bin_ver;
  uint32_t bin_size;
  uint32_t header_offset;
  uint32_t data_offset;
  uint32_t data_size;
};

struct nvfw_bl_desc {
  uint32_t start_tag;
  uint32_t dmem_load_off;
  uint32_t code_off;
  uint32_t code_size;
  uint32_t data_off;
  uint32_t data_size;
};


int nvFalconLoadBl(NvFalconBootloader *bl, const char *path) {
  void *buf = NULL;
  size_t size = 0;

  if (fs_read_file(path, &buf, &size) != 0 || !buf || size < sizeof(struct nvfw_bin_hdr)) {
    kprintf("[NV] Failed to read bootloader: %s\n", path);
    return -1;
  }

  const struct nvfw_bin_hdr *hdr = (const struct nvfw_bin_hdr *)buf;
  if (hdr->bin_magic != 0x000010de && hdr->bin_magic != 0x3b1d14f0) {
    kprintf("[NV] %s: Invalid bin magic 0x%08x\n", path, hdr->bin_magic);
    kfree(buf);
    return -1;
  }

  if (hdr->header_offset + sizeof(struct nvfw_bl_desc) > size ||
      hdr->data_offset + hdr->data_size > size) {
    kprintf("[NV] %s: Truncated binary image\n", path);
    kfree(buf);
    return -1;
  }

  const struct nvfw_bl_desc *bld = (const struct nvfw_bl_desc *)((uint8_t *)buf + hdr->header_offset);
  bl->raw = buf;
  bl->rawSize = size;
  bl->code = (const uint8_t *)buf + hdr->data_offset + bld->code_off;
  bl->codeSize = bld->code_size;
  bl->bootAddr = bld->start_tag << 8;

  kprintf("[NV] Bootloader %s: entry 0x%x, size 0x%x\n", path, bl->bootAddr, bl->codeSize);
  return 0;
}

void nvFalconFreeBl(NvFalconBootloader *bl) {
if (bl->raw) {
  kfree(bl->raw);
  bl->raw = NULL;
  bl->rawSize = 0;
  bl->code = NULL;
  bl->codeSize = 0;
  bl->bootAddr = 0;
}
}
