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
      return (uint32_t)(i + 2);
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
  if (romSize == 0) {
    kprintf("ROM size is 0\n");
    romSize = 512 * 1024;
  }

  dev->bios.data = buf;
  dev->bios.size = romSize;
  dev->bios.bitOffset = nvVbiosFindBit(dev->bios.data, romSize);

  kprintf("[NV] VBIOS loaded %u KB; BIT offset: 0x%x\n", (uint32_t)(romSize / 1024), dev->bios.bitOffset);

  return 0;
}


void nvVbiosFree(NvDevice *dev) {
  if (dev->bios.data) {
    kfree(dev->bios.data);
    dev->bios.data = NULL;
    dev->bios.size = 0;
    dev->bios.bitOffset = 0;
  }
}
