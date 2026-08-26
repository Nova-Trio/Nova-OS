#include <nv_falcon.h>

void nv_falcon_init(NvFalcon *falcon, uint32_t base_addr, uint32_t fbif_base, uint32_t riscv_base, bool is_riscv, const char *name) {
  if (!falcon) {
    return;
  }
  falcon->base_addr = base_addr;
  falcon->fbif_base = fbif_base;
  falcon->riscv_base = riscv_base;
  falcon->is_riscv = is_riscv;
  falcon->name = name ? name : "UnknownFalcon";
}

void nv_falcon_init_gsp(NvFalcon *falcon) {
  nv_falcon_init(falcon, NV_FALCON_GSP_BASE, NV_FALCON_GSP_FBIF_BASE, NV_FALCON_GSP_RISCV_BASE, true, "GSP");
}

void nv_falcon_init_sec2(NvFalcon *falcon) {
  nv_falcon_init(falcon, NV_FALCON_SEC2_BASE, NV_FALCON_SEC2_FBIF_BASE, 0, false, "SEC2");
}

int nv_falcon_wait_mem_scrubbing(const NvDevice *dev, const NvFalcon *falcon) {
  if (!dev || !falcon || !dev->bar0.virt_addr) {
    return -1;
  }

  uint32_t timeout = NV_FALCON_DEFAULT_TIMEOUT_US;
  while (timeout > 0) {
    uint32_t dmactl = nv_rd32(dev, falcon->base_addr + NV_FALCON_DMACTL);
    if ((dmactl & (NV_FALCON_DMACTL_DMEM_SCRUBBING | NV_FALCON_DMACTL_IMEM_SCRUBBING)) == 0) {
      return 0;
    }
    hpet_sleep_us(5);
    if (timeout >= 5) {
      timeout -= 5;
    } else {
      break;
    }
  }

  kprintf("[NV/FLCN] Error: Timeout waiting for memory scrubbing on %s Falcon\n", falcon->name);
  return -1;
}

int nv_falcon_reset(const NvDevice *dev, const NvFalcon *falcon) {
  if (!dev || !falcon || !dev->bar0.virt_addr) {
    return -1;
  }

  nv_wr32(dev, falcon->base_addr + NV_FALCON_ENGINE_RESET, NV_FALCON_ENGINE_RESET_TRUE);

  for (uint32_t i = 0; i < NV_FALCON_RESET_PROPAGATION_CYCLES; i++) {
    (void)nv_rd32(dev, falcon->base_addr + NV_FALCON_ENGINE_RESET);
  }

  nv_wr32(dev, falcon->base_addr + NV_FALCON_ENGINE_RESET, NV_FALCON_ENGINE_RESET_FALSE);

  for (uint32_t i = 0; i < NV_FALCON_RESET_PROPAGATION_CYCLES; i++) {
    (void)nv_rd32(dev, falcon->base_addr + NV_FALCON_ENGINE_RESET);
  }

  int res = nv_falcon_wait_mem_scrubbing(dev, falcon);
  if (res != 0) {
    return res;
  }

  uint32_t dmactl = nv_rd32(dev, falcon->base_addr + NV_FALCON_DMACTL);
  dmactl &= ~NV_FALCON_DMACTL_REQUIRE_CTX;
  nv_wr32(dev, falcon->base_addr + NV_FALCON_DMACTL, dmactl);

  uint32_t fbif_ctl = nv_rd32(dev, falcon->fbif_base + NV_FALCON_FBIF_CTL);
  fbif_ctl |= NV_FALCON_FBIF_CTL_ALLOW_PHYS_NO_CTX;
  nv_wr32(dev, falcon->fbif_base + NV_FALCON_FBIF_CTL, fbif_ctl);

  return 0;
}

int nv_falcon_imem_write(const NvDevice *dev, const NvFalcon *falcon, uint32_t target_imem_addr, const uint8_t *src, size_t size, bool secure, uint32_t tag) {
  if (!dev || !falcon || !dev->bar0.virt_addr || !src || size == 0) {
    return -1;
  }

  if ((target_imem_addr % NV_FALCON_IMEM_BLK_SIZE) != 0) {
    kprintf("[NV/FLCN] Error: IMEM target addr 0x%x not block aligned on %s\n", target_imem_addr, falcon->name);
    return -1;
  }

  uint32_t num_words = (uint32_t)((size + 3) / 4);
  uint32_t cur_tag = tag >> 8;

  uint32_t imemc = (target_imem_addr & (NV_FALCON_IMEMC_OFFS_MASK | NV_FALCON_IMEMC_BLK_MASK)) | NV_FALCON_IMEMC_AINCW | (secure ? NV_FALCON_IMEMC_SECURE : 0);

  nv_wr32(dev, falcon->base_addr + NV_FALCON_IMEMC(0), imemc);

  for (uint32_t i = 0; i < num_words; i++) {
    if ((i & ((NV_FALCON_IMEM_BLK_SIZE / 4) - 1)) == 0) {
      nv_wr32(dev, falcon->base_addr + NV_FALCON_IMEMT(0), cur_tag & NV_FALCON_IMEMT_TAG_MASK);
      cur_tag++;
    }

    uint32_t dword = 0;
    size_t bytes_left = size - (i * 4);
    if (bytes_left >= 4) {
      memcpy(&dword, src + (i * 4), 4);
    } else {
      memcpy(&dword, src + (i * 4), bytes_left);
    }

    nv_wr32(dev, falcon->base_addr + NV_FALCON_IMEMD(0), dword);
  }

  return 0;
}

int nv_falcon_dmem_write(const NvDevice *dev, const NvFalcon *falcon, uint32_t target_dmem_addr, const uint8_t *src, size_t size) {
  if (!dev || !falcon || !dev->bar0.virt_addr || !src || size == 0) {
    return -1;
  }

  if ((target_dmem_addr % NV_FALCON_DMEM_ALIGN_SIZE) != 0) {
    kprintf("[NV/FLCN] Error: DMEM target addr 0x%x not 4-byte aligned on %s\n", target_dmem_addr, falcon->name);
    return -1;
  }

  uint32_t num_words = (uint32_t)((size + 3) / 4);

  uint32_t dmemc = (target_dmem_addr & (NV_FALCON_DMEMC_OFFS_MASK | NV_FALCON_DMEMC_BLK_MASK)) | NV_FALCON_DMEMC_AINCW;

  nv_wr32(dev, falcon->base_addr + NV_FALCON_DMEMC(0), dmemc);

  for (uint32_t i = 0; i < num_words; i++) {
    uint32_t dword = 0;
    size_t bytes_left = size - (i * 4);
    if (bytes_left >= 4) {
      memcpy(&dword, src + (i * 4), 4);
    } else {
      memcpy(&dword, src + (i * 4), bytes_left);
    }

    nv_wr32(dev, falcon->base_addr + NV_FALCON_DMEMD(0), dword);
  }

  return 0;
}

int nv_falcon_dmem_read(const NvDevice *dev, const NvFalcon *falcon, uint32_t target_dmem_addr, uint8_t *dst, size_t size) {
  if (!dev || !falcon || !dev->bar0.virt_addr || !dst || size == 0) {
    return -1;
  }

  if ((target_dmem_addr % NV_FALCON_DMEM_ALIGN_SIZE) != 0) {
    return -1;
  }

  uint32_t num_words = (uint32_t)((size + 3) / 4);

  uint32_t dmemc = (target_dmem_addr & (NV_FALCON_DMEMC_OFFS_MASK | NV_FALCON_DMEMC_BLK_MASK)) | NV_FALCON_DMEMC_AINCR;

  nv_wr32(dev, falcon->base_addr + NV_FALCON_DMEMC(0), dmemc);

  for (uint32_t i = 0; i < num_words; i++) {
    uint32_t dword = nv_rd32(dev, falcon->base_addr + NV_FALCON_DMEMD(0));
    size_t bytes_left = size - (i * 4);
    if (bytes_left >= 4) {
      memcpy(dst + (i * 4), &dword, 4);
    } else {
      memcpy(dst + (i * 4), &dword, bytes_left);
    }
  }

  return 0;
}

int nv_falcon_setup_fbif_aperture(const NvDevice *dev, const NvFalcon *falcon,
                                  uint32_t dma_idx, uint32_t target, uint32_t mem_type) {
  if (!dev || !falcon || !dev->bar0.virt_addr || dma_idx >= 8) {
    return -1;
  }

  // disable context requirement for physical DMA
  uint32_t dmactl = nv_rd32(dev, falcon->base_addr + NV_FALCON_DMACTL);
  dmactl &= ~NV_FALCON_DMACTL_REQUIRE_CTX;
  nv_wr32(dev, falcon->base_addr + NV_FALCON_DMACTL, dmactl);

  uint32_t fbif_ctl = nv_rd32(dev, falcon->fbif_base + NV_FALCON_FBIF_CTL);
  fbif_ctl |= NV_FALCON_FBIF_CTL_ALLOW_PHYS_NO_CTX;
  nv_wr32(dev, falcon->fbif_base + NV_FALCON_FBIF_CTL, fbif_ctl);

  // program aperture target and memory type
  uint32_t transcfg = (target & 0x3) | (mem_type ? NV_FALCON_FBIF_TRANSCFG_MEM_TYPE_PHYSICAL : 0);
  nv_wr32(dev, falcon->fbif_base + NV_FALCON_FBIF_TRANSCFG(dma_idx), transcfg);

  return 0;
}

void nv_falcon_set_bootvec(const NvDevice *dev, const NvFalcon *falcon, uint32_t bootvec) {
  if (!dev || !falcon || !dev->bar0.virt_addr) {
    return;
  }
  nv_wr32(dev, falcon->base_addr + NV_FALCON_BOOTVEC, bootvec);
}

void nv_falcon_start_cpu(const NvDevice *dev, const NvFalcon *falcon) {
  if (!dev || !falcon || !dev->bar0.virt_addr) {
    return;
  }
  uint32_t cpuctl = nv_rd32(dev, falcon->base_addr + NV_FALCON_CPUCTL);
  if (cpuctl & NV_FALCON_CPUCTL_ALIAS_EN) {
    nv_wr32(dev, falcon->base_addr + 0x00000130U, NV_FALCON_CPUCTL_STARTCPU);
  } else {
    nv_wr32(dev, falcon->base_addr + NV_FALCON_CPUCTL, NV_FALCON_CPUCTL_STARTCPU);
  }
}

int nv_falcon_wait_halt(const NvDevice *dev, const NvFalcon *falcon, uint32_t timeout_us) {
  if (!dev || !falcon || !dev->bar0.virt_addr) {
    return -1;
  }

  if (timeout_us == 0) {
    timeout_us = NV_FALCON_DEFAULT_TIMEOUT_US;
  }

  while (timeout_us > 0) {
    uint32_t cpuctl = nv_rd32(dev, falcon->base_addr + NV_FALCON_CPUCTL);
    if (cpuctl & NV_FALCON_CPUCTL_HALTED) {
      // clear HALT interrupt
      nv_falcon_clear_intr(dev, falcon, NV_FALCON_IRQSCLR_HALT_SET);
      return 0;
    }

    hpet_sleep_us(10);
    if (timeout_us >= 10) {
      timeout_us -= 10;
    } else {
      break;
    }
  }

  kprintf("[NV/FLCN] Error: Timeout waiting for halt on %s Falcon (CPUCTL=0x%08x)\n", falcon->name, nv_rd32(dev, falcon->base_addr + NV_FALCON_CPUCTL));
  return -1;
}

void nv_falcon_clear_intr(const NvDevice *dev, const NvFalcon *falcon, uint32_t mask) {
  if (!dev || !falcon || !dev->bar0.virt_addr) {
    return;
  }
  nv_wr32(dev, falcon->base_addr + NV_FALCON_IRQSCLR, mask);
}

void nv_falcon_mailbox_write(const NvDevice *dev, const NvFalcon *falcon, uint8_t mbox_idx, uint32_t val) {
  if (!dev || !falcon || !dev->bar0.virt_addr) {
    return;
  }
  if (mbox_idx == 0) {
    nv_wr32(dev, falcon->base_addr + NV_FALCON_MAILBOX0, val);
  } else if (mbox_idx == 1) {
    nv_wr32(dev, falcon->base_addr + NV_FALCON_MAILBOX1, val);
  }
}

uint32_t nv_falcon_mailbox_read(const NvDevice *dev, const NvFalcon *falcon, uint8_t mbox_idx) {
  if (!dev || !falcon || !dev->bar0.virt_addr) {
    return 0;
  }
  if (mbox_idx == 0) {
    return nv_rd32(dev, falcon->base_addr + NV_FALCON_MAILBOX0);
  } else if (mbox_idx == 1) {
    return nv_rd32(dev, falcon->base_addr + NV_FALCON_MAILBOX1);
  }
  return 0;
}

void nv_falcon_set_os(const NvDevice *dev, const NvFalcon *falcon, uint32_t os_version) {
  if (!dev || !falcon || !dev->bar0.virt_addr) {
    return;
  }
  nv_wr32(dev, falcon->base_addr + NV_FALCON_OS, os_version);
}

bool nv_falcon_is_riscv_active(const NvDevice *dev, const NvFalcon *falcon) {
  if (!dev || !falcon || !falcon->is_riscv || !falcon->riscv_base || !dev->bar0.virt_addr) {
    return false;
  }
  uint32_t status = nv_rd32(dev, falcon->riscv_base + NV_PRISCV_RISCV_CORE_SWITCH_STATUS);
  return (status & NV_PRISCV_RISCV_CORE_SWITCH_ACTIVE) != 0;
}

int nv_falcon_reset_into_riscv(const NvDevice *dev, const NvFalcon *falcon) {
  if (!dev || !falcon || !falcon->is_riscv || !falcon->riscv_base || !dev->bar0.virt_addr) {
    return -1;
  }

  if (nv_falcon_reset(dev, falcon) != 0) {
    return -1;
  }

  // Clear and configure RISC-V CPU control
  nv_wr32(dev, falcon->riscv_base + NV_PRISCV_RISCV_CPUCTL, 0x00000000U);
  nv_wr32(dev, falcon->riscv_base + NV_PRISCV_RISCV_CPUCTL, NV_PRISCV_RISCV_CPUCTL_STARTCPU);

  uint32_t timeout = 100000U;
  while (timeout > 0) {
    if (nv_falcon_is_riscv_active(dev, falcon)) {
      return 0;
    }
    hpet_sleep_us(10);
    if (timeout >= 10) {
      timeout -= 10;
    } else {
      break;
    }
  }

  kprintf("[NV/FLCN] Error: Failed to activate RISC-V core on %s Falcon\n", falcon->name);
  return -1;
}
