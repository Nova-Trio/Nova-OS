#include <nv_gsp.h>
#include <nv_device.h>
#include <nv_dma.h>
#include <novamod.h>
#include <nv_falcon.h>
#include <nv_rpc.h>

#define ALIGN_UP(x, a) (((x) + ((a) - 1ULL)) & ~((a) - 1ULL))

static int gsp_extract_elf(NvGspContext *gsp, const uint8_t *elf_data, size_t elf_size, const char *sig_section_name) {
  if (!elf_data || elf_size < sizeof(Elf64_Ehdr)) {
    kprintf("[NV/GSP/FW] Error: Invalid GSP ELF data buffer\n");
    return -1;
  }

  const Elf64_Ehdr *ehdr = (const Elf64_Ehdr *)elf_data;
  if (memcmp(ehdr->e_ident, ELFMAG, SELFMAG) != 0 || ehdr->e_ident[4] != ELFCLASS64) {
    kprintf("[NV/GSP/FW] Error: GSP binary is not a valid 64-bit ELF image\n");
    return -1;
  }

  if (ehdr->e_shoff >= elf_size || (ehdr->e_shoff + (ehdr->e_shnum * sizeof(Elf64_Shdr))) > elf_size) {
    kprintf("[NV/GSP/FW] Error: Corrupted ELF section header table\n");
    return -1;
  }

  const Elf64_Shdr *shdr_table = (const Elf64_Shdr *)(elf_data + ehdr->e_shoff);
  if (ehdr->e_shstrndx >= ehdr->e_shnum) {
    kprintf("[NV/GSP/FW] Error: Invalid string table section index in ELF\n");
    return -1;
  }

  const Elf64_Shdr *shstrtab_hdr = &shdr_table[ehdr->e_shstrndx];
  if (shstrtab_hdr->sh_offset >= elf_size || (shstrtab_hdr->sh_offset + shstrtab_hdr->sh_size) > elf_size) {
    kprintf("[NV/GSP/FW] Error: Corrupted string table bounds in ELF\n");
    return -1;
  }

  const char *shstrtab = (const char *)(elf_data + shstrtab_hdr->sh_offset);

  const uint8_t *fw_img_src = NULL;
  size_t fw_img_size = 0;
  const uint8_t *sig_src = NULL;
  size_t sig_size = 0;

  for (size_t i = 0; i < ehdr->e_shnum; i++) {
    const Elf64_Shdr *shdr = &shdr_table[i];
    if (shdr->sh_name >= shstrtab_hdr->sh_size) {
      continue;
    }

    const char *section_name = shstrtab + shdr->sh_name;

    if (strcmp(section_name, GSP_FW_IMAGE_SECTION) == 0) {
      if (shdr->sh_offset + shdr->sh_size > elf_size) {
        kprintf("[NV/GSP/FW] Error: Out of bounds .fwimage section\n");
        return -1;
      }
      fw_img_src = elf_data + shdr->sh_offset;
      fw_img_size = shdr->sh_size;
    } else if (strcmp(section_name, sig_section_name) == 0) {
      if (shdr->sh_offset + shdr->sh_size > elf_size) {
        kprintf("[NV/GSP/FW] Error: Out of bounds signature section %s\n", sig_section_name);
        return -1;
      }
      sig_src = elf_data + shdr->sh_offset;
      sig_size = shdr->sh_size;
    }
  }

  if (!fw_img_src || fw_img_size == 0) {
    kprintf("[NV/GSP/FW] Error: Required section %s not found in GSP ELF\n", GSP_FW_IMAGE_SECTION);
    return -1;
  }

  if (!sig_src || sig_size == 0) {
    kprintf("[NV/GSP/FW] Error: Required signature section %s not found in GSP ELF\n", sig_section_name);
    return -1;
  }

  size_t aligned_sig_size = ALIGN_UP(sig_size, 256ULL);
  if (nv_dma_alloc(&gsp->sig, aligned_sig_size) != 0) {
    kprintf("[NV/GSP/FW] Error: Failed to allocate signature DMA buffer\n");
    return -1;
  }

  memcpy((void *)gsp->sig.virt_addr, sig_src, sig_size);

  kprintf("[NV/GSP/FW] Extracted %s (%u KB) and %s (%u bytes)\n", GSP_FW_IMAGE_SECTION, (uint32_t)(fw_img_size / 1024), sig_section_name, (uint32_t)sig_size);

  gsp->radix3.fw_raw_buffer = (void *)fw_img_src;
  gsp->radix3.fw_size = fw_img_size;
  return 0;
}

static int gsp_extract_bootloader(NvGspContext *gsp, const uint8_t *bl_data, size_t bl_size) {
  if (!bl_data || bl_size < sizeof(NvGspBinHdr)) {
    kprintf("[NV/GSP/FW] Error: Invalid bootloader buffer\n");
    return -1;
  }

  const NvGspBinHdr *hdr = (const NvGspBinHdr *)bl_data;
  //kprintf("[NV/GSP/FW] RAW HDR: magic=0x%08x ver=%u total_size=%u hdr_off=0x%x data_off=0x%x (desc_len=%u)\n", hdr->bin_magic, hdr->bin_ver, hdr->bin_size, hdr->header_offset, hdr->data_offset,
  //        (hdr->data_offset - hdr->header_offset));

  if ((hdr->header_offset + sizeof(NvGspBootloaderDesc)) > bl_size) {
    kprintf("[NV/GSP/FW] Error: Bootloader descriptor offset out of bounds\n");
    return -1;
  }

  if ((hdr->data_offset + hdr->data_size) > bl_size) {
    kprintf("[NV/GSP/FW] Error: Bootloader payload offset out of bounds\n");
    return -1;
  }

  const NvGspBootloaderDesc *desc = (const NvGspBootloaderDesc *)(bl_data + hdr->header_offset);
  memcpy(&gsp->boot_desc, desc, sizeof(NvGspBootloaderDesc));

  if (nv_dma_alloc(&gsp->boot_fw, hdr->data_size) != 0) {
    kprintf("[NV/GSP/FW] Error: Failed to allocate bootloader DMA buffer\n");
    return -1;
  }

  memcpy((void *)gsp->boot_fw.virt_addr, bl_data + hdr->data_offset, hdr->data_size);

  kprintf("[NV/GSP/FW] Bootloader loaded: AppVer=0x%08x, Code=0x%04x (size %u), Data=0x%04x (size %u), Manifest=0x%04x (size %u)\n", gsp->boot_desc.appVersion, gsp->boot_desc.monitorCodeOffset,
          gsp->boot_desc.monitorCodeSize, gsp->boot_desc.monitorDataOffset, gsp->boot_desc.monitorDataSize, gsp->boot_desc.manifestOffset, gsp->boot_desc.manifestSize);

  return 0;
}

static int gsp_build_radix3_tree(NvGspRadix3 *rx3) {
  if (!rx3->fw_raw_buffer || rx3->fw_size == 0) {
    return -1;
  }

  rx3->fw_page_count = (rx3->fw_size + GSP_PAGE_SIZE - 1ULL) / GSP_PAGE_SIZE;
  rx3->fw_page_phys = (uint64_t *)kmalloc(rx3->fw_page_count * sizeof(uint64_t));
  if (!rx3->fw_page_phys) {
    kprintf("[NV/GSP/FW] Error: Failed to allocate Radix-3 frame tracking list\n");
    return -1;
  }

  const uint8_t *src_payload = (const uint8_t *)rx3->fw_raw_buffer;

  for (size_t i = 0; i < rx3->fw_page_count; i++) {
    void *frame = pmm_alloc_frame();
    if (!frame) {
      kprintf("[NV/GSP/FW] Error: OOM allocating physical frame for GSP FW page %u\n", (uint32_t)i);
      for (size_t j = 0; j < i; j++) {
        pmm_free_frame((void *)rx3->fw_page_phys[j]);
      }
      kfree(rx3->fw_page_phys);
      rx3->fw_page_phys = NULL;
      return -1;
    }

    uint64_t phys_addr = (uint64_t)frame;
    rx3->fw_page_phys[i] = phys_addr;

    uint8_t *dst = (uint8_t *)(phys_addr + HHDM_BASE);
    size_t offset = i * GSP_PAGE_SIZE;
    size_t chunk = (i == (rx3->fw_page_count - 1)) ? (rx3->fw_size - offset) : GSP_PAGE_SIZE;

    memcpy(dst, src_payload + offset, chunk);
    if (chunk < GSP_PAGE_SIZE) {
      memset(dst + chunk, 0, GSP_PAGE_SIZE - chunk);
    }
  }

  size_t lvl2_bytes = ALIGN_UP(rx3->fw_page_count * sizeof(uint64_t), GSP_PAGE_SIZE);
  size_t lvl2_pages = lvl2_bytes / GSP_PAGE_SIZE;

  if (nv_dma_alloc(&rx3->lvl0, GSP_PAGE_SIZE) != 0 || nv_dma_alloc(&rx3->lvl1, GSP_PAGE_SIZE) != 0 || nv_dma_alloc(&rx3->lvl2, lvl2_bytes) != 0) {
    kprintf("[NV/GSP/FW] Error: Failed to allocate Radix-3 table pages\n");
    for (size_t i = 0; i < rx3->fw_page_count; i++) {
      pmm_free_frame((void *)rx3->fw_page_phys[i]);
    }
    kfree(rx3->fw_page_phys);
    rx3->fw_page_phys = NULL;
    return -1;
  }

  uint64_t *lvl0 = (uint64_t *)rx3->lvl0.virt_addr;
  memset(lvl0, 0, GSP_PAGE_SIZE);
  lvl0[0] = rx3->lvl1.phys_addr;

  uint64_t *lvl1 = (uint64_t *)rx3->lvl1.virt_addr;
  memset(lvl1, 0, GSP_PAGE_SIZE);
  for (size_t i = 0; i < lvl2_pages; i++) {
    lvl1[i] = rx3->lvl2.phys_addr + (i * GSP_PAGE_SIZE);
  }

  uint64_t *lvl2 = (uint64_t *)rx3->lvl2.virt_addr;
  memset(lvl2, 0, lvl2_bytes);
  for (size_t k = 0; k < rx3->fw_page_count; k++) {
    lvl2[k] = rx3->fw_page_phys[k];
  }

  nv_dma_wmb();

  kprintf("[NV/GSP/FW] Radix-3 tree built: %u FW pages across %u L2 pages (L0=0x%016llx, L1=0x%016llx, L2=0x%016llx)\n", (uint32_t)rx3->fw_page_count, (uint32_t)lvl2_pages, rx3->lvl0.phys_addr,
          rx3->lvl1.phys_addr, rx3->lvl2.phys_addr);

  return 0;
}

static int gsp_setup_libos_buffers(NvGspContext *gsp) {
  if (nv_dma_alloc(&gsp->loginit, GSP_LOG_BUFFER_SIZE) != 0 || nv_dma_alloc(&gsp->logintr, GSP_LOG_BUFFER_SIZE) != 0 || nv_dma_alloc(&gsp->logrm,   GSP_LOG_BUFFER_SIZE) != 0) {
    kprintf("[NV/GSP/FW] Error: Failed to allocate LibOS logging buffers\n");
    return -1;
  }

  NvDmaBuffer *log_bufs[3] = { &gsp->loginit, &gsp->logintr, &gsp->logrm };
  for (size_t b = 0; b < 3; b++) {
    NvDmaBuffer *buf = log_bufs[b];
    uint64_t *virt = (uint64_t *)buf->virt_addr;
    virt[0] = 0ULL;

    uint64_t *ptes = (uint64_t *)(buf->virt_addr + sizeof(uint64_t));
    for (size_t i = 0; i < (GSP_LOG_BUFFER_SIZE / GSP_PAGE_SIZE); i++) {
      ptes[i] = buf->phys_addr + (i * GSP_PAGE_SIZE);
    }
  }

  if (nv_dma_alloc(&gsp->rmargs, GSP_RMARGS_SIZE) != 0 || nv_dma_alloc(&gsp->libos,  GSP_LIBOS_ARGS_SIZE) != 0) {
    kprintf("[NV/GSP/FW] Error: Failed to allocate LibOS arguments pages\n");
    return -1;
  }

  LibosMemoryRegionInitArgument *args = (LibosMemoryRegionInitArgument *)gsp->libos.virt_addr;
  memset(args, 0, GSP_LIBOS_ARGS_SIZE);

  args[0].id8 = LIBOS_ID_LOGINIT;
  args[0].pa = gsp->loginit.phys_addr;
  args[0].size = gsp->loginit.size;
  args[0].kind = LIBOS_MEMORY_REGION_CONTIGUOUS;
  args[0].loc = LIBOS_MEMORY_REGION_LOC_SYSMEM;

  args[1].id8 = LIBOS_ID_LOGINTR;
  args[1].pa = gsp->logintr.phys_addr;
  args[1].size = gsp->logintr.size;
  args[1].kind = LIBOS_MEMORY_REGION_CONTIGUOUS;
  args[1].loc = LIBOS_MEMORY_REGION_LOC_SYSMEM;

  args[2].id8 = LIBOS_ID_LOGRM;
  args[2].pa = gsp->logrm.phys_addr;
  args[2].size = gsp->logrm.size;
  args[2].kind = LIBOS_MEMORY_REGION_CONTIGUOUS;
  args[2].loc = LIBOS_MEMORY_REGION_LOC_SYSMEM;

  args[3].id8 = LIBOS_ID_RMARGS;
  args[3].pa = gsp->rmargs.phys_addr;
  args[3].size = gsp->rmargs.size;
  args[3].kind = LIBOS_MEMORY_REGION_CONTIGUOUS;
  args[3].loc = LIBOS_MEMORY_REGION_LOC_SYSMEM;

  nv_dma_wmb();
  return 0;
}

static int gsp_setup_shm_queues(NvGspContext *gsp) {
  if (nv_dma_alloc(&gsp->shm, GSP_SHM_TOTAL_SIZE) != 0) {
    kprintf("[NV/GSP/FW] Error: Failed to allocate SHM message queue backing store\n");
    return -1;
  }

  uint64_t *ptes = (uint64_t *)gsp->shm.virt_addr;
  for (size_t i = 0; i < GSP_SHM_TOTAL_PAGES; i++) {
    ptes[i] = gsp->shm.phys_addr + (i * GSP_PAGE_SIZE);
  }

  NvGspMsgqTxHeader *cmdq_tx = (NvGspMsgqTxHeader *)(gsp->shm.virt_addr + GSP_SHM_CMDQ_OFFSET);
  NvGspMsgqRxHeader *cmdq_rx = (NvGspMsgqRxHeader *)(gsp->shm.virt_addr + GSP_SHM_CMDQ_OFFSET + sizeof(NvGspMsgqTxHeader));
  memset(cmdq_tx, 0, sizeof(*cmdq_tx));
  memset(cmdq_rx, 0, sizeof(*cmdq_rx));

  cmdq_tx->version  = 0;
  cmdq_tx->size = GSP_SHM_CMDQ_SIZE;
  cmdq_tx->msgSize = GSP_PAGE_SIZE;
  cmdq_tx->entryOff = GSP_PAGE_SIZE;
  cmdq_tx->msgCount = (GSP_SHM_CMDQ_SIZE - GSP_PAGE_SIZE) / GSP_PAGE_SIZE;
  cmdq_tx->writePtr = 0;
  cmdq_tx->flags = 1;
  cmdq_tx->rxHdrOff = (uint32_t)sizeof(NvGspMsgqTxHeader);

  NvGspMsgqTxHeader *msgq_tx = (NvGspMsgqTxHeader *)(gsp->shm.virt_addr + GSP_SHM_MSGQ_OFFSET);
  NvGspMsgqRxHeader *msgq_rx = (NvGspMsgqRxHeader *)(gsp->shm.virt_addr + GSP_SHM_MSGQ_OFFSET + sizeof(NvGspMsgqTxHeader));
  memset(msgq_tx, 0, sizeof(*msgq_tx));
  memset(msgq_rx, 0, sizeof(*msgq_rx));

  gsp->cmdq_wptr = &cmdq_tx->writePtr;
  gsp->cmdq_rptr = &msgq_rx->readPtr;
  gsp->msgq_wptr = &msgq_tx->writePtr;
  gsp->msgq_rptr = &cmdq_rx->readPtr;
  gsp->cmdq_seq = 0;

  GspArgumentsCached *cached_args = (GspArgumentsCached *)gsp->rmargs.virt_addr;
  memset(cached_args, 0, sizeof(*cached_args));

  cached_args->messageQueueInitArguments.sharedMemPhysAddr = gsp->shm.phys_addr;
  cached_args->messageQueueInitArguments.pageTableEntryCount = (uint32_t)GSP_SHM_TOTAL_PAGES;
  cached_args->messageQueueInitArguments.cmdQueueOffset = GSP_SHM_CMDQ_OFFSET;
  cached_args->messageQueueInitArguments.statQueueOffset = GSP_SHM_MSGQ_OFFSET;

  cached_args->srInitArguments.oldLevel = 0;
  cached_args->srInitArguments.flags = 0;
  cached_args->srInitArguments.bInPMTransition = 0;

  nv_dma_wmb();

  kprintf("[NV/GSP/FW] Shared Message Queues initialized: 129 PTEs, cmdq=0x%llx (rxHdrOff=%u), msgq=0x%llx\n", gsp->shm.phys_addr + GSP_SHM_CMDQ_OFFSET, cmdq_tx->rxHdrOff,
          gsp->shm.phys_addr + GSP_SHM_MSGQ_OFFSET);

  return 0;
}

int nv_gsp_fw_stage_all(const NvDevice *dev, NvGspContext *gsp) {
  if (!dev || !gsp) {
    return -1;
  }

  memset(gsp, 0, sizeof(*gsp));

  const char *sig_sec = GSP_FW_SIGNATURE_SECTION_TU10X;
  if (dev->chip.chipset >= 0x167 && dev->chip.chipset <= 0x168) {
    sig_sec = GSP_FW_SIGNATURE_SECTION_TU11X;
  }

  void *raw_gsp = NULL;
  size_t gsp_size = 0;
  if (fs_read_file("/nova/fw/gsp.bin", &raw_gsp, &gsp_size) != 0 || !raw_gsp) {
    kprintf("[NV/GSP/FW] Error: Failed to read /nova/fw/gsp.bin from disk\n");
    return -1;
  }

  if (gsp_extract_elf(gsp, (const uint8_t *)raw_gsp, gsp_size, sig_sec) != 0) {
    kfree(raw_gsp);
    nv_gsp_fw_cleanup(gsp);
    return -1;
  }

  if (gsp_build_radix3_tree(&gsp->radix3) != 0) {
    kfree(raw_gsp);
    nv_gsp_fw_cleanup(gsp);
    return -1;
  }

  kfree(raw_gsp);

  void *raw_bl = NULL;
  size_t bl_size = 0;
  if (fs_read_file("/nova/fw/bootloader.bin", &raw_bl, &bl_size) != 0 || !raw_bl) {
    kprintf("[NV/GSP/FW] Error: Failed to read /nova/fw/bootloader.bin from disk\n");
    nv_gsp_fw_cleanup(gsp);
    return -1;
  }

  if (gsp_extract_bootloader(gsp, (const uint8_t *)raw_bl, bl_size) != 0) {
    kfree(raw_bl);
    nv_gsp_fw_cleanup(gsp);
    return -1;
  }

  kfree(raw_bl);

  if (gsp_setup_libos_buffers(gsp) != 0) {
    nv_gsp_fw_cleanup(gsp);
    return -1;
  }

  if (gsp_setup_shm_queues(gsp) != 0) {
    nv_gsp_fw_cleanup(gsp);
    return -1;
  }

  if (nv_dma_alloc(&gsp->wpr_meta, 0x1000) != 0) {
    kprintf("[NV/GSP/FW] Error: Failed to allocate WPR metadata buffer\n");
    nv_gsp_fw_cleanup(gsp);
    return -1;
  }

  kprintf("[NV/GSP/FW] Host Fw & DMA Staging completed.\n");
  return 0;
}

void nv_gsp_fw_cleanup(NvGspContext *gsp) {
  if (!gsp) {
    return;
  }

  if (gsp->wpr_meta.phys_addr) nv_dma_free(&gsp->wpr_meta);
  if (gsp->shm.phys_addr) nv_dma_free(&gsp->shm);
  if (gsp->libos.phys_addr) nv_dma_free(&gsp->libos);
  if (gsp->rmargs.phys_addr) nv_dma_free(&gsp->rmargs);
  if (gsp->logrm.phys_addr) nv_dma_free(&gsp->logrm);
  if (gsp->logintr.phys_addr) nv_dma_free(&gsp->logintr);
  if (gsp->loginit.phys_addr) nv_dma_free(&gsp->loginit);

  if (gsp->radix3.lvl2.phys_addr) nv_dma_free(&gsp->radix3.lvl2);
  if (gsp->radix3.lvl1.phys_addr) nv_dma_free(&gsp->radix3.lvl1);
  if (gsp->radix3.lvl0.phys_addr) nv_dma_free(&gsp->radix3.lvl0);

  if (gsp->radix3.fw_page_phys) {
    for (size_t i = 0; i < gsp->radix3.fw_page_count; i++) {
      if (gsp->radix3.fw_page_phys[i]) {
        pmm_free_frame((void *)gsp->radix3.fw_page_phys[i]);
      }
    }
    kfree(gsp->radix3.fw_page_phys);
    gsp->radix3.fw_page_phys = NULL;
  }

  if (gsp->boot_fw.phys_addr) nv_dma_free(&gsp->boot_fw);
  if (gsp->sig.phys_addr)     nv_dma_free(&gsp->sig);

  memset(gsp, 0, sizeof(*gsp));
}

__attribute__((warning("Verify functions are empty on master branch")))
int nv_gsp_fw_verify_test(const NvDevice *dev, const NvGspContext *gsp) {
  (void)dev;
  (void)gsp;
  return 0;
}

int nv_gsp_push_preinit_rpc(const NvDevice *dev, NvGspContext *gsp, uint32_t fn, const void *payload, size_t payload_size) {
  if (!dev || !gsp || !gsp->cmdq_wptr || !payload || payload_size == 0) {
    return -1;
  }

  uint32_t cur_wptr = *gsp->cmdq_wptr;
  size_t total_payload = sizeof(NvGspRpcHdr) + payload_size;
  size_t total_msg_size = sizeof(NvGspMsgElemHdr) + total_payload;
  size_t aligned_size = ALIGN_UP(total_msg_size, GSP_PAGE_SIZE);

  uint8_t *slot = (uint8_t *)gsp->shm.virt_addr + GSP_SHM_CMDQ_OFFSET + GSP_PAGE_SIZE + (cur_wptr * GSP_PAGE_SIZE);
  memset(slot, 0, aligned_size);

  NvGspMsgElemHdr *elem = (NvGspMsgElemHdr *)slot;
  elem->sequence = gsp->cmdq_seq++;
  elem->elem_count = (uint32_t)(aligned_size / GSP_PAGE_SIZE);
  elem->pad = 0;
  elem->checksum = 0;

  NvGspRpcHdr *rpc = (NvGspRpcHdr *)(slot + sizeof(NvGspMsgElemHdr));
  rpc->header_version = GSP_RPC_HEADER_VERSION;
  rpc->signature = GSP_RPC_SIGNATURE;
  rpc->length = (uint32_t)total_payload;
  rpc->function = fn;
  rpc->rpc_result = 0xFFFFFFFFU;
  rpc->rpc_result_private = 0xFFFFFFFFU;
  rpc->sequence = 0;
  rpc->cpu_rm_gfid = 0;

  memcpy(slot + sizeof(NvGspMsgElemHdr) + sizeof(NvGspRpcHdr), payload, payload_size);

  uint64_t csum = 0;
  const uint64_t *p = (const uint64_t *)slot;
  const uint64_t *end = (const uint64_t *)(slot + aligned_size);
  while (p < end) {
    csum ^= *p++;
  }
  elem->checksum = (uint32_t)(csum >> 32) ^ (uint32_t)(csum & 0xFFFFFFFFU);

  uint32_t next_wptr = (cur_wptr + elem->elem_count) % 63;
  *gsp->cmdq_wptr = next_wptr;
  nv_dma_wmb();
  nv_dma_mb();

  nv_wr32(dev, NV_FALCON_GSP_BASE + 0x00000C00U, 0x00000000U);
  return 0;
}

int nv_gsp_submit_preinit_sequence(const NvDevice *dev, NvGspContext *gsp) {
  NvGspSystemInfoPayload sys_info;
  memset(&sys_info, 0, sizeof(sys_info));
  sys_info.gpu_phys_addr = dev->bar0.phys_addr;
  sys_info.gpu_phys_fb_addr = dev->bar1.phys_addr;
  sys_info.gpu_phys_inst_addr = dev->bar3.phys_addr ? dev->bar3.phys_addr : 0;
  sys_info.nv_domain_bus_device_func = ((uint64_t)dev->pci_dev->bus << 8) |
  ((uint64_t)dev->pci_dev->device << 3) |
  (uint64_t)dev->pci_dev->function;
  sys_info.max_user_va = 0x1FFFFFFFFFFFULL;
  sys_info.chipset = dev->chip.chipset;

  sys_info.pci_config_mirror_base = 0x00088000U;
  sys_info.pci_config_mirror_size = 0x00001000U;

  if (nv_gsp_push_preinit_rpc(dev, gsp, NV_VGPU_MSG_FUNCTION_GSP_SET_SYSTEM_INFO, &sys_info, sizeof(sys_info)) != 0) {
    return -1;
  }

  const char *k0 = "RMSecBusResetEnable";
  const char *k1 = "RMForcePcieConfigSave";
  const char *k2 = "RMDevidCheckIgnore";

  size_t k0_len = strlen(k0) + 1;
  size_t k1_len = strlen(k1) + 1;
  size_t k2_len = strlen(k2) + 1;

  size_t str_pool_offset = 8 + (3 * sizeof(NvGspPackedRegEntry));
  size_t total_reg_size = str_pool_offset + k0_len + k1_len + k2_len;

  uint8_t reg_buf[256];
  memset(reg_buf, 0, sizeof(reg_buf));

  uint32_t *hdr_size = (uint32_t *)(reg_buf + 0);
  uint32_t *hdr_num  = (uint32_t *)(reg_buf + 4);
  *hdr_size = (uint32_t)total_reg_size;
  *hdr_num  = 3;

  NvGspPackedRegEntry *entries = (NvGspPackedRegEntry *)(reg_buf + 8);

  entries[0].name_offset = (uint32_t)str_pool_offset;
  entries[0].type = GSP_REGISTRY_TYPE_DWORD;
  entries[0].data = 1;
  entries[0].length = 4;
  memcpy(reg_buf + str_pool_offset, k0, k0_len);
  str_pool_offset += k0_len;

  entries[1].name_offset = (uint32_t)str_pool_offset;
  entries[1].type = GSP_REGISTRY_TYPE_DWORD;
  entries[1].data = 1;
  entries[1].length = 4;
  memcpy(reg_buf + str_pool_offset, k1, k1_len);
  str_pool_offset += k1_len;

  entries[2].name_offset = (uint32_t)str_pool_offset;
  entries[2].type = GSP_REGISTRY_TYPE_DWORD;
  entries[2].data = 1;
  entries[2].length = 4;
  memcpy(reg_buf + str_pool_offset, k2, k2_len);

  if (nv_gsp_push_preinit_rpc(dev, gsp, NV_VGPU_MSG_FUNCTION_SET_REGISTRY, reg_buf, total_reg_size) != 0) {
    return -1;
  }

  return 0;
}
