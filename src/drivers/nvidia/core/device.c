#include <nv_device.h>
#include <nv_reg.h>
#include <stdint.h>
#include <turing.h>
#include <nv_bus.h>
#include <nv_mmu.h>

#define PCI_VENDOR_NVIDIA 0x10DE

static NvDevice *g_devices = NULL;

// Only when an architecture is supported add it here
static const char* nv_decode_chip_name(uint16_t chipset){
  switch (chipset){
    case 0x162: return "TU102";
    case 0x164: return "TU104";
    case 0x166: return "TU106";
    case 0x167: return "TU117";
    case 0x168: return "TU116";
    default: return "Unknown / Not yet supported NVIDIA";
  }
}

static void nv_identify_chip(NvDevice* dev){
  uint32_t boot0 = nv_rd32(dev, NV_PMC_BOOT_0);
  dev->chip.raw_boot0 = boot0;
  dev->chip.minor_rev = (uint8_t)((boot0 & NV_PMC_BOOT_0_MINOR_REV_MASK) >> NV_PMC_BOOT_0_MINOR_REV_SHIFT);
  dev->chip.major_rev = (uint8_t)((boot0 & NV_PMC_BOOT_0_MAJOR_REV_MASK) >> NV_PMC_BOOT_0_MAJOR_REV_SHIFT);
  dev->chip.chipset = (uint16_t)((boot0 & NV_PMC_BOOT_0_CHIPSET_MASK) >> NV_PMC_BOOT_0_CHIPSET_SHIFT);
  dev->chip.chip_name = nv_decode_chip_name(dev->chip.chipset);

  if (dev->chip.chipset >= NV_CHIPSET_TURING_MIN && dev->chip.chipset <= NV_CHIPSET_TURING_MAX) {
    dev->chip.arch = NV_ARCH_TURING;
    dev->chip.arch_name = "Turing";
    dev->ops = &g_turing_ops;
  } else {
    dev->chip.arch = NV_ARCH_UNKNOWN;
    dev->chip.arch_name = "Unsupported";
    dev->ops = NULL;
  }
}

static int nv_map_bar(NvBar *bar, uint64_t phys_addr, uint64_t size) {
  if (!phys_addr || !size) {
    return -1;
  }

  uint64_t virt_addr = phys_addr + HHDM_BASE;
  PageDirectory pml4 = vmm_get_kernel_pml4();

  if (vmm_map_range(pml4, virt_addr, phys_addr, size, VMM_FLAG_PRESENT | VMM_FLAG_WRITABLE | VMM_FLAG_NO_CACHE) != 0) {
    return -1;
  }

  bar->phys_addr = phys_addr;
  bar->virt_addr = virt_addr;
  bar->size = size;
  return 0;
}

static void nv_unmap_bar(NvBar *bar) {
  if (bar->virt_addr && bar->size) {
    PageDirectory pml4 = vmm_get_kernel_pml4();
    vmm_unmap_range(pml4, bar->virt_addr, bar->size);
    bar->virt_addr = 0;
    bar->phys_addr = 0;
    bar->size = 0;
  }
}

static int nv_device_init_single(const PciDevice *pci_dev) {
  if (pci_dev->bars[0].size == 0 || pci_dev->bars[0].phys_addr == 0) {
    kprintf("[NVIDIA] Error: BAR0 not present on %02x:%02x.%u\n",
            (uint32_t)pci_dev->bus, (uint32_t)pci_dev->device, (uint32_t)pci_dev->function);
    return -1;
  }

  NvDevice *dev = (NvDevice *)kzalloc(sizeof(NvDevice));
  if (!dev) {
    return -1;
  }

  dev->pci_dev = pci_dev;
  pcie_enable_bus_master(pci_dev);

  if (nv_map_bar(&dev->bar0, pci_dev->bars[0].phys_addr, pci_dev->bars[0].size) != 0) {
    kprintf("[NVIDIA] Error: Failed to map BAR0\n");
    kfree(dev);
    return -1;
  }

  if (pci_dev->bars[1].size > 0 && pci_dev->bars[1].phys_addr > 0) {
    nv_map_bar(&dev->bar1, pci_dev->bars[1].phys_addr, pci_dev->bars[1].size);
  }

  if (pci_dev->bars[3].size > 0 && pci_dev->bars[3].phys_addr > 0) {
    nv_map_bar(&dev->bar3, pci_dev->bars[3].phys_addr, pci_dev->bars[3].size);
  }

  nv_identify_chip(dev);

  kprintf("[NVIDIA] Discovered %s (%s) [PCI %02x:%02x.%u | DevID: 0x%04x | BOOT0: 0x%08x]\n",
          dev->chip.chip_name, dev->chip.arch_name,
          (uint32_t)pci_dev->bus, (uint32_t)pci_dev->device, (uint32_t)pci_dev->function,
          (uint32_t)pci_dev->device_id, dev->chip.raw_boot0);

  if (!dev->ops) {
    kprintf("[NVIDIA] Architecture %s is currently unsupported\n", dev->chip.arch_name);
    nv_unmap_bar(&dev->bar3);
    nv_unmap_bar(&dev->bar1);
    nv_unmap_bar(&dev->bar0);
    kfree(dev);
    return -1;
  }

  int res = dev->ops->init(dev);
  if (res != 0) {
    kprintf("[NVIDIA] Error: Ops init failed for %s (%d)\n", dev->chip.chip_name, res);
    nv_unmap_bar(&dev->bar3);
    nv_unmap_bar(&dev->bar1);
    nv_unmap_bar(&dev->bar0);
    kfree(dev);
    return res;
  }

  dev->next = g_devices;
  g_devices = dev;
  return 0;
}

int nv_device_probe(void) {
  size_t count = pcie_get_device_count();
  int found = 0;

  for (size_t i = 0; i < count; i++) {
    const PciDevice *pci_dev = pcie_get_device(i);
    if (pci_dev && pci_dev->vendor_id == PCI_VENDOR_NVIDIA && pci_dev->class_code == 0x03) {
      if (nv_device_init_single(pci_dev) == 0) {
        found++;
      }
    }
  }

  return (found > 0) ? 0 : -1;
}

void nv_device_remove_all(void) {
  NvDevice *curr = g_devices;
  while (curr) {
    NvDevice *next = curr->next;
    if (curr->ops && curr->ops->cleanup) {
      curr->ops->cleanup(curr);
    }
    if (curr->bar1_inst.phys_addr) {
      nv_dma_free(&curr->bar1_inst);
    }
    nv_unmap_bar(&curr->bar3);
    nv_unmap_bar(&curr->bar1);
    nv_unmap_bar(&curr->bar0);
    kfree(curr);
    curr = next;
  }
  g_devices = NULL;
}

#define NV_PBUS_BIND_STATUS 0x00001710

static void nv_bus_wait_bar1_bind(const NvDevice *dev) {
  for (uint32_t i = 0; i < 1000000; i++) {
    if ((nv_rd32(dev, NV_PRAMIN_BAR1_STATUS) & NV_PRAMIN_BAR1_STATUS_BUSY) == 0) {
      break;
    }
    __asm__ volatile("pause");
  }
}

#define NV_PBUS_BAR0_WINDOW 0x00001700
#define NV_PRAMIN_OFFSET    0x00700000

int nv_bus_bind_bar1_vmm(NvDevice *dev, const NvVmm *vmm) {
  if (!dev || !dev->bar0.virt_addr || !vmm || !vmm->pdb.phys_addr) {
    return -1;
  }

  nv_wr32(dev, NV_PBUS_BAR0_WINDOW, 0x00000000);

  nv_wr32(dev, NV_PRAMIN_BAR1_BLOCK, nv_rd32(dev, NV_PRAMIN_BAR1_BLOCK) & ~NV_PRAMIN_BAR1_BLOCK_ENABLE);
  nv_dma_wmb();
  nv_bus_wait_bar1_bind(dev);

  uint64_t base = BIT_ULL(10) /* VER2 */ | BIT_ULL(11) /* 64KiB */ | (2ULL << 0) /* HOST */ | BIT_ULL(2) /* VOL */;
  base |= vmm->pdb.phys_addr;

  uint32_t data0 = (uint32_t)(base & 0xFFFFFFFFU);
  uint32_t data1 = (uint32_t)(base >> 32);
  uint64_t limit = dev->bar1.size ? (dev->bar1.size - 1) : 0x0FFFFFFFULL;

  volatile uint32_t *inst = (volatile uint32_t *)((uint8_t *)dev->bar0.virt_addr + NV_PRAMIN_OFFSET);
  memset((void *)inst, 0, 0x1000);

  inst[0x200 / 4] = data0;
  inst[0x204 / 4] = data1;
  inst[0x208 / 4] = (uint32_t)(limit & 0xFFFFFFFFU);
  inst[0x20c / 4] = (uint32_t)(limit >> 32);
  inst[0x21c / 4] = 0x00000000;
  inst[0x298 / 4] = 0x00000001; // Lower 32 bits mask
  inst[0x29c / 4] = 0x00000000; // Upper 32 bits mask

  for (size_t i = 0; i < 64; i++) {
    if (i == 0) {
      inst[(0x2a0 + i * 0x10) / 4] = data0;
      inst[(0x2a4 + i * 0x10) / 4] = data1;
    } else {
      inst[(0x2a0 + i * 0x10) / 4] = 0x00000001;
      inst[(0x2a4 + i * 0x10) / 4] = 0x00000001;
    }
    inst[(0x2a8 + i * 0x10) / 4] = 0x00000000;
  }

  nv_dma_wmb();

  nv_wr32(dev, NV_PRAMIN_BAR1_BLOCK, NV_PRAMIN_BAR1_BLOCK_ENABLE | 0x00000000);
  nv_dma_wmb();

  nv_bus_wait_bar1_bind(dev);

  nv_mmu_tlb_invalidate(dev, vmm->pdb.phys_addr);
  return 0;
}

int nv_bus_bind_bar1_phys(const NvDevice *dev, uint64_t phys_addr, uint32_t target) {
  if (!dev || !dev->bar0.virt_addr) {
    return -1;
  }

  uint32_t ptr_val = (uint32_t)(phys_addr >> NV_PBUS_BAR1_BLOCK_PTR_ALIGN_SHIFT);
  uint32_t val = (NV_PBUS_BAR1_BLOCK_MODE_PHYSICAL << NV_PBUS_BAR1_BLOCK_MODE_SHIFT) |
  ((target & 0x3) << NV_PBUS_BAR1_BLOCK_TARGET_SHIFT) |
  (ptr_val & NV_PBUS_BAR1_BLOCK_PTR_MASK);

  nv_wr32(dev, NV_PBUS_BAR1_BLOCK, val);
  nv_dma_wmb();
  nv_bus_wait_bar1_bind(dev);
  return 0;
}

// Again yall can guard the krpintfs
void nv_mmu_tlb_invalidate(const NvDevice *dev, uint64_t pdb_phys) {
  if (!dev || !dev->bar0.virt_addr) {
    return;
  }

  kprintf("[NV/MMU] TLB invalidate: PDB=0x%016llx\n", pdb_phys);

  nv_wr32(dev, NV_PFB_PRI_MMU_INVALIDATE_PDB_LO, (uint32_t)(pdb_phys >> 8));
  nv_wr32(dev, NV_PFB_PRI_MMU_INVALIDATE_PDB_HI, (uint32_t)(pdb_phys >> 40));
  nv_wr32(dev, NV_PFB_PRI_MMU_INVALIDATE_CMD, NV_PFB_PRI_MMU_INVALIDATE_TRIGGER | NV_PFB_PRI_MMU_INVALIDATE_ALL);
  nv_dma_wmb();

  uint32_t timeout = 1000000;
  uint32_t status;
  do {
    status = nv_rd32(dev, NV_PFB_PRI_MMU_INVALIDATE_CMD);
    if ((status & NV_PFB_PRI_MMU_INVALIDATE_TRIGGER) == 0) {
      kprintf("[NV/MMU] TLB invalidate succeeded (status=0x%08x)\n", status);
      return;
    }
    __asm__ volatile("pause");
  } while (--timeout > 0);

  // Timeout
  kprintf("[NV/MMU] TLB invalidate TIMEOUT! last status=0x%08x\n", status);
}
