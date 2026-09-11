// big H
#include "nvidia/device.h"
#include <novamod.h>
#include <stddef.h>
#include <stdint.h>
#include <nvrm/nvtypes.h>

static NvDevice* gDevices = NULL;

static int nvMapBar(NvBar* bar, uint64_t phys, uint64_t size){
  if(!phys || !size) return -1;

  uint64_t virt = phys + HHDM_BASE;
  PageDirectory pml4 = vmm_get_kernel_pml4();

  if(vmm_map_range(pml4, virt, phys, size, VMM_FLAG_PRESENT | VMM_FLAG_WRITABLE | VMM_FLAG_NO_CACHE) != 0){
    return -1;
  }

  bar->physAddr = phys;
  bar->virtAddr = virt;
  bar->size = size;
  return 0;
}

static void nvUnmapBar(NvBar* bar){
  if(bar->virtAddr && bar->size){
    PageDirectory pml4 = vmm_get_kernel_pml4();
    vmm_unmap_range(pml4, bar->virtAddr, bar->size);
    bar->virtAddr = 0;
    bar->physAddr = 0;
    bar->size = 0;
  
  }
}

int nvRealInit(const PciDevice* dev){
  NvDevice* gpu = (NvDevice*)kzalloc(sizeof(NvDevice));
  if(!gpu) return -1;

  gpu->dev = dev;

  pcie_enable_bus_master(dev);
  uint16_t cmd = pcie_read16(dev->segment, dev->bus, dev->device, dev->function, PCI_REG_COMMAND);
  cmd |= PCI_CMD_MEM_ENABLE;
  pcie_write16(dev->segment, dev->bus, dev->device, dev->function, PCI_REG_COMMAND, cmd);

  if (nvMapBar(&gpu->bar0, dev->bars[0].phys_addr, dev->bars[0].size) != 0) {
    kfree(gpu);
    return -1;
  }

  uint32_t boot0 = nvRd32(gpu, 0x0000000);
  if (boot0 == 0xFFFFFFFF || boot0 == 0x00000000) {
    kprintf("[NV] Failed to read PMC_BOOT_0: invalid response 0x%08x\n", boot0);
    nvUnmapBar(&gpu->bar0);
    kfree(gpu);
    return -1;
  }

  if((boot0 & 0x1f000000) > 0){
    gpu->chipset = (boot0 & 0x1ff00000) >> 20;
    gpu->chiprev = (boot0 & 0x000000ff);
    switch (gpu->chipset & 0x1f0) {
      case 0x160: gpu->gpuArch = NV_TU100; break;
    }

    switch (gpu->chipset){
      case 0x162: gpu->gpuArch = NV_TU102; break;
      case 0x164: gpu->gpuArch = NV_TU104; break;
      case 0x166: gpu->gpuArch = NV_TU106; break;
      case 0x167: gpu->gpuArch = NV_TU117; break;
      case 0x168: gpu->gpuArch = NV_TU116; break;
      default:
        nvUnmapBar(&gpu->bar0);
        kfree(gpu);
        return -1;
    }
  } 

  uint32_t boot1 = nvRd32(gpu, 0x000004);
  if(boot1 & 0x0030000){
    kprintf("vGPU is not supported!\n");
    nvUnmapBar(&gpu->bar0);
    kfree(gpu);
    return -1;
  }

  uint32_t strap = nvRd32(gpu, 0x101000);
  switch (strap & 0x00400040) {
    case 0x00000000: gpu->crystal = 13500; break;
    case 0x00000040: gpu->crystal = 14318; break;
    case 0x00400000: gpu->crystal = 27000; break;
    case 0x00400040: gpu->crystal = 25000; break;
  }

  gpu->next = gDevices;
  gDevices = gpu;

  return 0;
}

int driver_init(void){
  size_t count = pcie_get_device_count();
  int found = 0;

  for (size_t i = 0; i < count; i++){
    const PciDevice* dev = pcie_get_device(i);
    if(dev && dev->vendor_id == 0x10DE && dev->class_code == 0x03){
      if(nvRealInit(dev) == 0){
        found++;
      }
    }
  }

  return (found > 0) ? 0 : -1;
}

void driver_exit(){
  NvDevice* curr = gDevices;
  while(curr){
    NvDevice* next = curr->next;
    nvUnmapBar(&curr->bar0);
    //nvUnmapBar(&curr->bar1);
    //nvUnmapBar(&curr->bar3);
    kfree(curr);
    curr = next;
  }
  gDevices = NULL;
}
