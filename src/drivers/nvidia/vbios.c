#include "device.h"
#include <novamod.h>
#include <stddef.h>
#include <stdint.h>

#define PCI_REG_ROM_ADDRESS 0x30
#define VBIOS_SIGNATURE 0xAA55
#define PCIR_SIGNATURE 0x52494350

static size_t nvVbiosGetTotalSize(const uint8_t *rom, size_t maxSize) {
  size_t offset = 0;

  while (offset + 0x20 <= maxSize) {
    if (*(const uint16_t *)(rom + offset) != VBIOS_SIGNATURE)
      break;

    uint16_t pcirOffset = *(const uint16_t *)(rom + offset + 0x18);
    if (offset + pcirOffset + 0x18 > maxSize)
      break;

    const uint8_t *pcir = rom + offset + pcirOffset;
    if (*(const uint32_t *)pcir != PCIR_SIGNATURE)
      break;

    uint16_t imageLenUnits = *(const uint16_t *)(pcir + 0x10);
    size_t imageSize = (size_t)imageLenUnits * 512;
    if (imageSize == 0 || offset + imageSize > maxSize)
      break;

    offset += imageSize;

    uint8_t indicator = *(const uint8_t *)(pcir + 0x15);
    if (indicator & 0x80)
      return offset;
  }

  return offset;
}

static int nvVbiosReadProm(NvDevice* dev, uint8_t* buf, size_t size){
  uint32_t sig = nvRd32(dev, 0x00300000);
  if((sig & 0xFFFF) != VBIOS_SIGNATURE) return -1;

  for(size_t offset = 0; offset < size; offset += 4){
    uint32_t val = nvRd32(dev, 0x00300000 + offset);
    size_t copyLen = (size - offset < 4) ? (size - offset) : 4;
    memcpy(buf + offset, &val, copyLen);
  }

  return 0;
}

static int nvVbiosReadPciRom(NvDevice* dev, uint8_t* buf, size_t size){
  const PciDevice *pci = dev->dev;
  uint32_t origRom = pcie_read32(pci->segment, pci->bus, pci->device, pci->function, PCI_REG_ROM_ADDRESS);

  uint64_t romPhys = origRom & 0xFFFFF800ULL;
  if (!romPhys)
    return -1;

  pcie_write32(pci->segment, pci->bus, pci->device, pci->function, PCI_REG_ROM_ADDRESS, (uint32_t)romPhys | 0x1);

  uint64_t romVirt = romPhys + HHDM_BASE;
  PageDirectory pml4 = vmm_get_kernel_pml4();

  if (vmm_map_range(pml4, romVirt, romPhys, size, VMM_FLAG_PRESENT | VMM_FLAG_WRITABLE | VMM_FLAG_NO_CACHE) != 0) {
    pcie_write32(pci->segment, pci->bus, pci->device, pci->function, PCI_REG_ROM_ADDRESS, origRom);
    return -1;
  }

  if (*(volatile uint16_t *)romVirt == VBIOS_SIGNATURE) {
    memcpy(buf, (const void *)romVirt, size);
    vmm_unmap_range(pml4, romVirt, size);
    pcie_write32(pci->segment, pci->bus, pci->device, pci->function, PCI_REG_ROM_ADDRESS, origRom);
    return 0;
  }

  vmm_unmap_range(pml4, romVirt, size);
  pcie_write32(pci->segment, pci->bus, pci->device, pci->function, PCI_REG_ROM_ADDRESS, origRom);
  return -1;
}

static uint32_t nvVbiosFindBit(const uint8_t *data, size_t size) {
  const uint8_t sig[5] = { 0xFF, 0xB8, 'B', 'I', 'T' };

  if (size < 5)
    return 0;

  for (size_t i = 0; i <= size - 5; i++) {
    if (data[i] == sig[0] &&
        data[i + 1] == sig[1] &&
        data[i + 2] == sig[2] &&
        data[i + 3] == sig[3] &&
        data[i + 4] == sig[4]) {
      return (uint32_t)i;
    }
  }

  return 0;
}

int nvVbiosInit(NvDevice *dev) {
  const size_t maxRomSize = 1024 * 1024;
  uint8_t *buf = (uint8_t *)kmalloc(maxRomSize);
  if (!buf)
    return -1;

  int ret = nvVbiosReadProm(dev, buf, maxRomSize);
  int useProm = (ret == 0);

  if (!useProm) {
    ret = nvVbiosReadPciRom(dev, buf, maxRomSize);
    if (ret != 0) {
      kfree(buf);
      kprintf("[NV] No valid VBIOS ROM found\n");
      return -1;
    }
  }

  if (*(uint16_t *)buf != VBIOS_SIGNATURE) {
    kfree(buf);
    kprintf("[NV] Invalid VBIOS signature: 0x%04x\n", *(uint16_t *)buf);
    return -1;
  }

  size_t romSize = nvVbiosGetTotalSize(buf, maxRomSize);
  if (romSize < 512 * 1024) {
    kprintf("ROM size is 0\n");
    romSize = 512 * 1024;
  }

  dev->bios.data = buf;
  dev->bios.size = romSize;
  dev->bios.bitOffset = nvVbiosFindBit(dev->bios.data, romSize);

  kprintf("[NV] VBIOS loaded %u KB; BIT offset: 0x%x\n", (uint32_t)(romSize / 1024), dev->bios.bitOffset);

  NvPmuEntry fwsec;
  if(nvVbiosFindFwsec(dev, &fwsec) == 0){
    uint32_t hdr = nvbiosRd32(dev, fwsec.data);
    uint32_t ver = (hdr & 0x0000FF00) >> 8;
    uint32_t siz = (hdr & 0xFFFF0000) >> 16;
    uint32_t imemLoadSize = nvbiosRd32(dev, fwsec.data + 0x18);
    uint32_t imemSecSize = nvbiosRd32(dev, fwsec.data + 0x24);
    uint32_t dmemOffset = nvbiosRd32(dev, fwsec.data + 0x28);
    uint32_t dmemLoadSize = nvbiosRd32(dev, fwsec.data + 0x30);
    uint32_t ifOffset = nvbiosRd32(dev, fwsec.data + 0x10);
    kprintf("[NV] FWSEC at 0x%x (v%u, header size: 0x%x)\n",fwsec.data,ver,siz);
    if(ver == 2){
      kprintf("[NV] FWSEC v2: IMEM: 0x%x (Sec: 0x%x), DMEM: 0x%x (Off: 0x%x), IF: 0x%x\n", 
        imemLoadSize, imemSecSize, dmemLoadSize, dmemOffset, ifOffset);
    }
  }else{
    kprintf("[NV] FWSEC not found\n");
  }

  return 0;
}

uint8_t nvbiosRd08(const NvDevice* dev, uint32_t addr) {
  if (addr >= dev->bios.size)
    return 0;
  return dev->bios.data[addr];
}

uint16_t nvbiosRd16(const NvDevice* dev, uint32_t addr) {
  if (addr + 2 > dev->bios.size)
    return 0;
  return *(const uint16_t *)(dev->bios.data + addr);
}

uint32_t nvbiosRd32(const NvDevice* dev, uint32_t addr) {
  if (addr + 4 > dev->bios.size)
    return 0;
  return *(const uint32_t *)(dev->bios.data + addr);
}

void* nvbiosPointer(const NvDevice* dev, uint32_t addr) {
  if (addr >= dev->bios.size)
    return NULL;
  return (void *)(dev->bios.data + addr);
}

int nvbiosBitEntry(const NvDevice *dev, uint8_t id, NvBitEntry *bit){
  if(!dev->bios.bitOffset) return -1;

  uint8_t entries = nvbiosRd08(dev, dev->bios.bitOffset + 10);
  uint8_t step = nvbiosRd08(dev, dev->bios.bitOffset + 9);
  if (!step) step = 6;

  uint32_t entry = dev->bios.bitOffset + 12;

  while (entries--){
    if (nvbiosRd08(dev, entry + 0) == id) {
      bit->id = nvbiosRd08(dev, entry + 0);
      bit->version = nvbiosRd08(dev, entry + 1);
      bit->length = nvbiosRd16(dev, entry + 2);
      bit->offset = nvbiosRd16(dev, entry + 4);
      return 0;
    }
    entry += step;
  }

  return -1;
}

uint32_t nvbiosPmuTe(const NvDevice* dev, uint8_t* ver, uint8_t* hdr, uint8_t* cnt, uint8_t* len) {
  NvBitEntry bitP;
  uint32_t data = 0;

  if (nvbiosBitEntry(dev, 'p', &bitP) == 0) {
    if (bitP.version == 2 && bitP.length >= 4)
      data = nvbiosRd32(dev, bitP.offset + 0x00);
    if (data) {
      *ver = nvbiosRd08(dev, data + 0x00);
      *hdr = nvbiosRd08(dev, data + 0x01);
      *len = nvbiosRd08(dev, data + 0x02);
      *cnt = nvbiosRd08(dev, data + 0x03);
    }
  }

  return data;
}

static uint32_t nvbiosPmuEe(const NvDevice* dev, int idx, uint8_t* ver, uint8_t* hdr) {
  uint8_t cnt, len;
  uint32_t data = nvbiosPmuTe(dev, ver, hdr, &cnt, &len);
  if (data && idx < cnt) {
    data = data + *hdr + (idx * len);
    *hdr = len;
    return data;
  }
  return 0;
}

uint32_t nvbiosPmuEp(const NvDevice* dev, int idx, uint8_t* ver, uint8_t* hdr, NvPmuEntry* info) {
  uint32_t data = nvbiosPmuEe(dev, idx, ver, hdr);
  if (data) {
    info->type = nvbiosRd08(dev, data + 0x00);
    info->data = nvbiosRd32(dev, data + 0x02);
  }
  return data;
}

int nvVbiosFindFwsec(const NvDevice *dev, NvPmuEntry *info) {
  uint8_t ver, hdr;
  for (int idx = 0; nvbiosPmuEp(dev, idx, &ver, &hdr, info); idx++) {
    if (info->type == 0x85) return 0;
  }
  return -1;
}

void nvVbiosFree(NvDevice *dev) {
  if (dev->bios.data) {
    kfree(dev->bios.data);
    dev->bios.data = NULL;
    dev->bios.size = 0;
    dev->bios.bitOffset = 0;
  }
}
