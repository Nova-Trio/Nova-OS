#include <novamod.h>
#include "virtioGpu.h"

static VirtioGpuDevice* gGpuDevice = NULL;

static int mapBar(VirtioGpuDevice *gpu, uint8_t barIndex) {
  if (barIndex >= gpu->pciDev->bar_count) return -1;
  if (gpu->bars[barIndex].mapped) return 0;

  const PciBar *pciBar = &gpu->pciDev->bars[barIndex];
  if (pciBar->is_io || pciBar->phys_addr == 0 || pciBar->size == 0) return -1;

  uint64_t physStart = pciBar->phys_addr & ~(PAGE_SIZE - 1);
  uint64_t virtStart = physStart + HHDM_BASE;

  PageDirectory pml4 = vmm_get_kernel_pml4();

  if (!vmm_virt_to_phys(pml4, virtStart)) {
    uint64_t physEnd = (pciBar->phys_addr + pciBar->size + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1);
    uint64_t mapSize = physEnd - physStart;

    if (vmm_map_range(pml4, virtStart, physStart, mapSize, VMM_FLAG_PRESENT | VMM_FLAG_WRITABLE | VMM_FLAG_NO_CACHE) != 0) {
      return -1;
    }
    gpu->bars[barIndex].size = mapSize;
  } else {
    gpu->bars[barIndex].size = pciBar->size;
  }

  gpu->bars[barIndex].physAddr = physStart;
  gpu->bars[barIndex].virtAddr = virtStart;
  gpu->bars[barIndex].mapped = 1;
  return 0;
}

static void unmapAllBars(VirtioGpuDevice *gpu) {
  PageDirectory pml4 = vmm_get_kernel_pml4();
  for (uint8_t i = 0; i < 6; i++) {
    if (gpu->bars[i].mapped) {
      vmm_unmap_range(pml4, gpu->bars[i].virtAddr, gpu->bars[i].size);
      gpu->bars[i].mapped = 0;
      gpu->bars[i].virtAddr = 0;
      gpu->bars[i].physAddr = 0;
      gpu->bars[i].size = 0;
    }
  }
}

static int enumerateCapabilities(VirtioGpuDevice *gpu) {
  const PciDevice *dev = gpu->pciDev;
  uint16_t status = pcie_read16(dev->segment, dev->bus, dev->device, dev->function, PCI_REG_STATUS);
  if (!(status & PCI_STATUS_CAPABILITIES)) {
    kprintf("[VIRTIO-GPU] Device does not support PCI capabilities\n");
    return -1;
  }

  uint8_t capPtr = pcie_read8(dev->segment, dev->bus, dev->device, dev->function, PCI_REG_CAPABILITIES) & ~0x3;

  while (capPtr >= 0x40 && capPtr <= 0xFC) {
    uint8_t capId = pcie_read8(dev->segment, dev->bus, dev->device, dev->function, capPtr);

    if (capId == 0x09) {
      VirtioPciCap cap;
      for (size_t i = 0; i < sizeof(VirtioPciCap); i++) {
        ((uint8_t *)&cap)[i] = pcie_read8(dev->segment, dev->bus, dev->device, dev->function, capPtr + (uint16_t)i);
      }

      if (cap.bar < dev->bar_count && mapBar(gpu, cap.bar) == 0) {
        uint64_t regVirt = gpu->bars[cap.bar].virtAddr + cap.offset;

        switch (cap.cfgType) {
          case VIRTIO_PCI_CAP_COMMON_CFG:
            if (cap.length >= sizeof(VirtioPciCommonCfg)) {
              gpu->commonCfg = (VirtioPciCommonCfg *)regVirt;
            }
            break;

          case VIRTIO_PCI_CAP_NOTIFY_CFG:
            gpu->notifyBase = (volatile void *)regVirt;
            gpu->notifyOffMultiplier = pcie_read32(dev->segment, dev->bus, dev->device, dev->function, capPtr + sizeof(VirtioPciCap));
            break;

          case VIRTIO_PCI_CAP_ISR_CFG:
            gpu->isrCfg = (volatile uint8_t *)regVirt;
            break;

          case VIRTIO_PCI_CAP_DEVICE_CFG:
            if (cap.length >= sizeof(VirtioGpuConfig)) {
              gpu->deviceCfg = (VirtioGpuConfig *)regVirt;
            }
            break;

          case VIRTIO_PCI_CAP_SHARED_MEMORY_CFG:
            gpu->adapter.mem.shmTotal += cap.length;
            gpu->adapter.mem.shmFree += cap.length;
            break;

          default:
            break;
        }
      }
    }

    capPtr = pcie_read8(dev->segment, dev->bus, dev->device, dev->function, capPtr + 1) & ~0x3;
  }

  if (!gpu->commonCfg || !gpu->notifyBase || !gpu->isrCfg || !gpu->deviceCfg) {
    kprintf("[VIRTIO-GPU] Missing mandatory VirtIO capabilities\n");
    return -1;
  }

  return 0;
}


static int waitForStatusZero(VirtioPciCommonCfg *cfg, uint64_t timeoutMs) {
  uint64_t start = hpet_get_millis();
  while (cfg->deviceStatus != 0) {
    if ((hpet_get_millis() - start) >= timeoutMs) {
      kprintf("[VIRTIO-GPU] Timeout waiting for device reset\n");
      return -1;
    }
    __asm__ volatile("pause");
  }
  return 0;
}

static int negotiateFeatures(VirtioGpuDevice *gpu) {
  VirtioPciCommonCfg *cfg = gpu->commonCfg;
  uint8_t status = 0;

  cfg->deviceStatus = 0;
  __asm__ volatile("mfence" ::: "memory");
  if (waitForStatusZero(cfg, 1000) != 0) {
    return -1;
  }

  status = VIRTIO_STATUS_ACKNOWLEDGE | VIRTIO_STATUS_DRIVER;
  cfg->deviceStatus = status;
  __asm__ volatile("mfence" ::: "memory");

  cfg->deviceFeatureSelect = 0;
  __asm__ volatile("mfence" ::: "memory");
  uint32_t featLow = cfg->deviceFeature;

  cfg->deviceFeatureSelect = 1;
  __asm__ volatile("mfence" ::: "memory");
  uint32_t featHigh = cfg->deviceFeature;

  gpu->hostFeatures = ((uint64_t)featHigh << 32) | featLow;

  if (!(gpu->hostFeatures & VIRTIO_F_VERSION_1)) {
    kprintf("[VIRTIO-GPU] Device does not offer VIRTIO_F_VERSION_1\n");
    status |= VIRTIO_STATUS_FAILED;
    cfg->deviceStatus = status;
    return -1;
  }

  uint64_t desiredFeatures = VIRTIO_F_VERSION_1;
  desiredFeatures |= (gpu->hostFeatures & VIRTIO_GPU_F_VIRGL);
  desiredFeatures |= (gpu->hostFeatures & VIRTIO_GPU_F_EDID);
  desiredFeatures |= (gpu->hostFeatures & VIRTIO_GPU_F_RESOURCE_UUID);
  desiredFeatures |= (gpu->hostFeatures & VIRTIO_GPU_F_RESOURCE_BLOB);
  desiredFeatures |= (gpu->hostFeatures & VIRTIO_GPU_F_CONTEXT_INIT);

  cfg->driverFeatureSelect = 0;
  cfg->driverFeature = (uint32_t)(desiredFeatures & 0xFFFFFFFF);
  __asm__ volatile("mfence" ::: "memory");

  cfg->driverFeatureSelect = 1;
  cfg->driverFeature = (uint32_t)(desiredFeatures >> 32);
  __asm__ volatile("mfence" ::: "memory");

  status |= VIRTIO_STATUS_FEATURES_OK;
  cfg->deviceStatus = status;
  __asm__ volatile("mfence" ::: "memory");

  if (!(cfg->deviceStatus & VIRTIO_STATUS_FEATURES_OK)) {
    kprintf("[VIRTIO-GPU] Device rejected negotiated features\n");
    status |= VIRTIO_STATUS_FAILED;
    cfg->deviceStatus = status;
    return -1;
  }

  gpu->negotiatedFeatures = desiredFeatures;
  return 0;
}

static int fillNagAdapter(VirtioGpuDevice *gpu) {
  struct GpuAdapter *adapter = &gpu->adapter;

  adapter->pciVendor = gpu->pciDev->vendor_id;
  adapter->pciDevice = gpu->pciDev->device_id;
  adapter->priv = gpu;

  adapter->caps = 0;

  uint32_t scanouts = gpu->deviceCfg->numScanouts;
  adapter->disp.headCount = scanouts;

  if (scanouts > 0) {
    adapter->caps |= NAG_CAP_SUPPORTS_2D | NAG_CAP_DISP_MODESET | NAG_CAP_DISP_HWCURSR;
    adapter->disp.heads = (struct DispHead *)kzalloc(scanouts * sizeof(struct DispHead));
    if (!adapter->disp.heads) {
      kprintf("[VIRTIO-GPU] Failed to allocate %u display heads\n", scanouts);
      return -1;
    }

    for (uint32_t i = 0; i < scanouts; i++) {
      adapter->disp.heads[i].headId = i;
      adapter->disp.heads[i].connected = 0;
      adapter->disp.heads[i].modeCount = 0;
      adapter->disp.heads[i].modes = NULL;
    }
  } else {
    adapter->disp.heads = NULL;
  }

  if (gpu->negotiatedFeatures & VIRTIO_GPU_F_VIRGL) {
    adapter->caps |= NAG_CAP_SUPPORTS_3D | NAG_CAP_SUPPORTS_COMP;
    if (gpu->negotiatedFeatures & VIRTIO_GPU_F_CONTEXT_INIT) {
      adapter->caps |= NAG_CAP_GPGPU;
    }
    memcpy(adapter->name, "VirtIO GPU (3D; unknown)", 25);
  } else {
    memcpy(adapter->name, "VirtIO GPU (2D)", 16);
  }

  if (gpu->pciDev->subclass == 0x00 && !gpu->pciDev->bars[0].is_io) {
    adapter->mem.vramTotal = gpu->pciDev->bars[0].size;
    adapter->mem.vramFree = gpu->pciDev->bars[0].size;
  } else {
    adapter->mem.vramTotal = 0;
    adapter->mem.vramFree = 0;
  }

  adapter->engineCount = 1;
  adapter->engines = (struct GpuEngine *)kzalloc(sizeof(struct GpuEngine));
  if (!adapter->engines) {
    if (adapter->disp.heads) {
      kfree(adapter->disp.heads);
      adapter->disp.heads = NULL;
    }
    return -1;
  }

  adapter->engines[0].engineId = 0;
  adapter->engines[0].type = NAG_GPU_ENGINE_TYPE_UN;
  memcpy(adapter->engines[0].name, "VirtIO Control Queue", 21);
  adapter->engines[0].engine = gpu;

  return 0;
}

int virtioGpuProbe(const PciDevice *pciDev) {
  VirtioGpuDevice *gpu = (VirtioGpuDevice *)kzalloc(sizeof(VirtioGpuDevice));
  if (!gpu) {
    return -1;
  }

  gpu->pciDev = pciDev;
  pcie_enable_bus_master(pciDev);

  if (enumerateCapabilities(gpu) != 0) {
    unmapAllBars(gpu);
    kfree(gpu);
    return -1;
  }

  if (negotiateFeatures(gpu) != 0) {
    unmapAllBars(gpu);
    kfree(gpu);
    return -1;
  }

  if (fillNagAdapter(gpu) != 0) {
    unmapAllBars(gpu);
    kfree(gpu);
    return -1;
  }

  if (nagRegisterAdapter(&gpu->adapter) != 0) {
    kprintf("[VIRTIO-GPU] Failed to register adapter with NAG\n");
    if (gpu->adapter.engines) {
      kfree(gpu->adapter.engines);
    }
    if (gpu->adapter.disp.heads) {
      kfree(gpu->adapter.disp.heads);
    }
    unmapAllBars(gpu);
    kfree(gpu);
    return -1;
  }

  gGpuDevice = gpu;
  kprintf("[VIRTIO-GPU] Registered NAG Adapter ID %u: \"%s\" (Scanouts: %u, Engines: %u)\n", gpu->adapter.adapterId, gpu->adapter.name, gpu->adapter.disp.headCount, gpu->adapter.engineCount);

  return 0;
}

void virtioGpuRemove(void) {
  if (!gGpuDevice) {
    return;
  }

  if (gGpuDevice->commonCfg) {
    gGpuDevice->commonCfg->deviceStatus = 0;
  }

  nagUnregisterAdapter(&gGpuDevice->adapter);

  if (gGpuDevice->adapter.engines) {
    kfree(gGpuDevice->adapter.engines);
    gGpuDevice->adapter.engines = NULL;
  }

  if (gGpuDevice->adapter.disp.heads) {
    kfree(gGpuDevice->adapter.disp.heads);
    gGpuDevice->adapter.disp.heads = NULL;
  }

  unmapAllBars(gGpuDevice);
  kfree(gGpuDevice);
  gGpuDevice = NULL;
}

int driver_init(void) {
  size_t count = pcie_get_device_count();
  for (size_t i = 0; i < count; i++) {
    const PciDevice *dev = pcie_get_device(i);
    if (dev && dev->vendor_id == VIRTIO_PCI_VENDOR_ID && dev->device_id == VIRTIO_PCI_DEVICE_ID) {
      return virtioGpuProbe(dev);
    }
  }

  kprintf("[VIRTIO-GPU] No compatible VirtIO GPU device found\n");
  return -1;
}

void driver_exit(void) {
  virtioGpuRemove();
}
