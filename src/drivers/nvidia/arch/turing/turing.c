#include "turing.h"
#include <nv_reg.h>
#include <nv_bus.h>
#include <nv_dma.h>
#include <nv_vmm.h>
#include <nv_bios.h>

// NOTE: Yall can guard the kprintfs and other stuff with defines but only if the impl works

static int turing_init(NvDevice *dev) {
  kprintf("[NV/TU] Initializing %s (%s, rev %u.%u)\n",
          dev->chip.chip_name, dev->chip.arch_name,
          (uint32_t)dev->chip.major_rev, (uint32_t)dev->chip.minor_rev);

  // Yall can remove this check
  nv_wr32(dev, NV_PBUS_SW_SCRATCH(0), 0xA55A1234U);
  if (nv_rd32(dev, NV_PBUS_SW_SCRATCH(0)) != 0xA55A1234U) {
    kprintf("[NV/TU] Error: BAR0 MMIO failure\n");
    return -1;
  }

  if (nv_bus_bind_bar1_phys(dev, 0x0, NV_PBUS_BAR1_BLOCK_TARGET_VID_MEM) != 0) {
    kprintf("[NV/TU] Error: Failed to bind BAR1 to physical VRAM\n");
    return -1;
  }

  if (nv_vmm_create(&dev->vmm, NV_VMM_DEFAULT_VA_START, NV_VMM_DEFAULT_VA_LIMIT) != 0) {
    kprintf("[NV/TU] Error: Failed to create device VMM address space\n");
    return -1;
  }

  nv_mmu_tlb_invalidate(dev, dev->vmm.pdb.phys_addr);

  kprintf("[NV/TU] PDB: 0x%016llx\n", dev->vmm.pdb.phys_addr);

  nv_bios_verify_test(dev);

  return 0;
}


static void turing_cleanup(NvDevice *dev) {
  kprintf("[NV/TU] Shutting down %s\n", dev->chip.chip_name);

  if (dev->vmm.pdb.phys_addr) {
    nv_mmu_tlb_invalidate(dev, dev->vmm.pdb.phys_addr);
    nv_vmm_destroy(&dev->vmm);
  }
}


const NvArchOps g_turing_ops = {
  .init = turing_init,
  .cleanup = turing_cleanup,
};
