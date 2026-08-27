#include "turing.h"
#include <nv_reg.h>
#include <nv_bus.h>
#include <nv_dma.h>
#include <nv_vmm.h>
#include <nv_bios.h>
#include <nv_falcon.h>
#include <nv_gsp.h>
#include <nv_wpr.h>
#include <nv_sec2.h>
#include <nv_rpc.h>
#include <string.h>

// NOTE: Yall can guard the kprintfs and other stuff with defines but only if the impl works

static int turing_rm_objects_init(NvDevice *dev, NvGspContext *gsp, const GspStaticConfigInfo *static_info) {
  int res;

  NV0000_ALLOC_PARAMETERS client_params;
  memset(&client_params, 0, sizeof(client_params));
  client_params.hClient = NV_RM_HANDLE_CLIENT;
  client_params.processID = 0;
  for (size_t i = 0; i < sizeof(client_params.processName) - 1 && "NovaKernel"[i]; i++) {
    client_params.processName[i] = "NovaKernel"[i];
  }

  res = nv_rm_alloc(dev, gsp, NV_RM_HANDLE_CLIENT, 0x00000000U, NV_RM_HANDLE_CLIENT, NV01_ROOT_CLIENT, &client_params, sizeof(client_params));
  if (res != 0) {
    kprintf("[NV/RM] Error: Failed to allocate Root Client (%d)\n", res);
    return res;
  }
  dev->rm_handles.client = NV_RM_HANDLE_CLIENT;
  kprintf("[NV/RM] Root Client allocated (Handle: 0x%08x)\n", dev->rm_handles.client);

  NV0080_ALLOC_PARAMETERS dev_params;
  memset(&dev_params, 0, sizeof(dev_params));
  dev_params.deviceId = 0;
  dev_params.hClientShare = dev->rm_handles.client;
  dev_params.hTargetClient = 0;
  dev_params.hTargetDevice = 0;
  dev_params.flags = 0;
  dev_params.vaMode = NV_DEVICE_ALLOCATION_VAMODE_MULTIPLE_VASPACES;

  res = nv_rm_alloc(dev, gsp, dev->rm_handles.client, dev->rm_handles.client, NV_RM_HANDLE_DEVICE, NV01_DEVICE_0, &dev_params, sizeof(dev_params));
  if (res != 0) {
    kprintf("[NV/RM] Error: Failed to allocate Device object (%d)\n", res);
    return res;
  }
  dev->rm_handles.device = NV_RM_HANDLE_DEVICE;
  kprintf("[NV/RM] Device allocated (Handle: 0x%08x)\n", dev->rm_handles.device);

  NV_VASPACE_ALLOCATION_PARAMETERS vas_params;
  memset(&vas_params, 0, sizeof(vas_params));
  vas_params.index = NV_VASPACE_ALLOCATION_INDEX_GPU_NEW;
  vas_params.flags = NV_VASPACE_ALLOCATION_FLAGS_IS_EXTERNALLY_OWNED;
  vas_params.vaBase = dev->vmm.va_start;
  vas_params.vaSize = dev->vmm.va_limit;
  vas_params.bigPageSize = 0;

  res = nv_rm_alloc(dev, gsp, dev->rm_handles.client, dev->rm_handles.device, NV_RM_HANDLE_VASPACE, FERMI_VASPACE_A, &vas_params, sizeof(vas_params));
  if (res != 0) {
    kprintf("[NV/RM] Error: Failed to allocate FERMI_VASPACE_A (%d)\n", res);
    return res;
  }
  dev->rm_handles.vaspace = NV_RM_HANDLE_VASPACE;
  kprintf("[NV/RM] FERMI_VASPACE_A allocated (Handle: 0x%08x)\n", dev->rm_handles.vaspace);

  NV0080_CTRL_DMA_SET_PAGE_DIRECTORY_PARAMS set_pdb_params;
  memset(&set_pdb_params, 0, sizeof(set_pdb_params));
  set_pdb_params.physAddress = dev->vmm.pdb.phys_addr;
  set_pdb_params.numEntries = 4;
  set_pdb_params.flags = NV0080_CTRL_DMA_SET_PAGE_DIRECTORY_FLAGS_APERTURE_SYSMEM_COH;
  set_pdb_params.hVASpace = dev->rm_handles.vaspace;
  set_pdb_params.subDeviceId = 0;

  res = nv_rm_control(dev, gsp, dev->rm_handles.client, dev->rm_handles.device, NV0080_CTRL_CMD_DMA_SET_PAGE_DIRECTORY, &set_pdb_params, sizeof(set_pdb_params));
  if (res != 0) {
    kprintf("[NV/RM] Error: Failed to bind host PDB to GSP-RM (%d)\n", res);
    return res;
  }
  kprintf("[NV/RM] Host PDB 0x%016llx registered with GSP-RM\n", dev->vmm.pdb.phys_addr);

  NV2080_CTRL_FIFO_GET_DEVICE_INFO_TABLE_PARAMS fifo_tbl;
  memset(&fifo_tbl, 0, sizeof(fifo_tbl));
  fifo_tbl.baseIndex = 0;

  res = nv_rm_control(dev, gsp, static_info->hInternalClient, static_info->hInternalSubdevice, NV2080_CTRL_CMD_FIFO_GET_DEVICE_INFO_TABLE, &fifo_tbl, sizeof(fifo_tbl));
  if (res != 0) {
    kprintf("[NV/RM] Error: Failed to query FIFO device info table (%d)\n", res);
    return res;
  }

  kprintf("\n[NV/FIFO] Discovered %u Hardware Engine Entries:\n", fifo_tbl.numEntries);
  for (uint32_t i = 0; i < fifo_tbl.numEntries; i++) {
    uint32_t engine_type = fifo_tbl.entries[i].engineData[ENGINE_INFO_TYPE_RM_ENGINE_TYPE];
    uint32_t runlist_id = fifo_tbl.entries[i].engineData[ENGINE_INFO_TYPE_RUNLIST];
    kprintf("[%02u] Engine: %-12s | RM Engine Type: 0x%04x | Runlist ID: %u | PBDMAs: %u\n", i, fifo_tbl.entries[i].engineName, engine_type, runlist_id, fifo_tbl.entries[i].numPbdmas);
  }
  kprintf("[NV/FIFO] **END**\n\n");

  return 0;
}



static void turing_execute_cpu_sequencer(const NvDevice *dev, const NvGspContext *gsp, uint8_t *slot) {
  uint32_t *seq_payload = (uint32_t *)(slot + sizeof(NvGspMsgElemHdr) + sizeof(NvGspRpcHdr));
  uint32_t cmd_idx = seq_payload[1];
  uint32_t *reg_save = seq_payload + 2;
  const uint32_t *cmd_buf = seq_payload + 10;

  NvFalcon gsp_flcn;
  nv_falcon_init_gsp(&gsp_flcn);

  NvFalcon sec2_flcn;
  nv_falcon_init_sec2(&sec2_flcn);

  uint32_t ptr = 0;
  while (ptr < cmd_idx) {
    uint32_t op = cmd_buf[ptr++];
    switch (op) {
      case 0: { // GSP_SEQ_BUF_OPCODE_REG_WRITE (addr, val)
        uint32_t addr = cmd_buf[ptr++];
        uint32_t val = cmd_buf[ptr++];
        nv_wr32(dev, addr, val);
        break;
      }
      case 1: { // GSP_SEQ_BUF_OPCODE_REG_MODIFY (addr, mask, val)
        uint32_t addr = cmd_buf[ptr++];
        uint32_t mask = cmd_buf[ptr++];
        uint32_t val = cmd_buf[ptr++];
        nv_wr32(dev, addr, (nv_rd32(dev, addr) & ~mask) | (val & mask));
        break;
      }
      case 2: { // GSP_SEQ_BUF_OPCODE_REG_POLL (addr, mask, val, timeout, error)
        uint32_t addr = cmd_buf[ptr++];
        uint32_t mask = cmd_buf[ptr++];
        uint32_t val = cmd_buf[ptr++];
        uint32_t timeout_us = cmd_buf[ptr++];
        ptr++; // error field
        if (timeout_us == 0) timeout_us = 4000000U;
        while (timeout_us > 0) {
          if ((nv_rd32(dev, addr) & mask) == val) break;
          hpet_sleep_us(5);
          if (timeout_us >= 5) timeout_us -= 5; else break;
        }
        break;
      }
      case 3: { // GSP_SEQ_BUF_OPCODE_DELAY_US (val)
        uint32_t us = cmd_buf[ptr++];
        hpet_sleep_us(us);
        break;
      }
      case 4: { // GSP_SEQ_BUF_OPCODE_REG_STORE (addr, index)
        uint32_t addr = cmd_buf[ptr++];
        uint32_t slot_idx = cmd_buf[ptr++];
        if (slot_idx < 8) {
          reg_save[slot_idx] = nv_rd32(dev, addr);
        }
        break;
      }
      case 5: { // GSP_SEQ_BUF_OPCODE_CORE_RESET
        nv_falcon_reset(dev, &gsp_flcn);
        uint32_t fbif_ctl = nv_rd32(dev, gsp_flcn.fbif_base + NV_FALCON_FBIF_CTL);
        fbif_ctl |= NV_FALCON_FBIF_CTL_ALLOW_PHYS_NO_CTX;
        nv_wr32(dev, gsp_flcn.fbif_base + NV_FALCON_FBIF_CTL, fbif_ctl);
        nv_wr32(dev, gsp_flcn.base_addr + NV_FALCON_DMACTL, 0);
        break;
      }
      case 6: { // GSP_SEQ_BUF_OPCODE_CORE_START
        if (nv_rd32(dev, gsp_flcn.base_addr + NV_FALCON_CPUCTL) & NV_FALCON_CPUCTL_ALIAS_EN) {
          nv_wr32(dev, gsp_flcn.base_addr + 0x130, 0x00000002);
        } else {
          nv_wr32(dev, gsp_flcn.base_addr + NV_FALCON_CPUCTL, NV_FALCON_CPUCTL_STARTCPU);
        }
        break;
      }
      case 7: { // GSP_SEQ_BUF_OPCODE_CORE_WAIT_FOR_HALT
        for (uint32_t t = 0; t < 200000; t++) {
          if (nv_rd32(dev, gsp_flcn.base_addr + NV_FALCON_CPUCTL) & NV_FALCON_CPUCTL_HALTED) break;
          hpet_sleep_us(10);
        }
        break;
      }
      case 8: { // GSP_SEQ_BUF_OPCODE_CORE_RESUME
        if (nv_falcon_reset_into_riscv(dev, &gsp_flcn) != 0) {
          kprintf("[NV/GSP] Error: Failed to reset GSP into RISC-V mode during CORE_RESUME\n");
        }

        uint32_t libos_lo = (uint32_t)(gsp->libos.phys_addr & 0xFFFFFFFFU);
        uint32_t libos_hi = (uint32_t)(gsp->libos.phys_addr >> 32);
        nv_falcon_mailbox_write(dev, &gsp_flcn, 0, libos_lo);
        nv_falcon_mailbox_write(dev, &gsp_flcn, 1, libos_hi);

        nv_falcon_start_cpu(dev, &sec2_flcn);

        uint32_t timeout_us = 2000000U;
        while (timeout_us > 0) {
          if ((nv_rd32(dev, 0x001180F8U) & 0x04000000U) != 0) {
            break;
          }
          hpet_sleep_us(10);
          if (timeout_us >= 10) timeout_us -= 10; else break;
        }

        if (timeout_us == 0) {
          kprintf("[NV/GSP] Error: Timed out waiting for 0x1180f8 & 0x04000000 handshake\n");
        }

        uint32_t mbox0 = nv_falcon_mailbox_read(dev, &sec2_flcn, 0);
        if (mbox0 != 0) {
          kprintf("[NV/GSP] Error: SEC2 returned error 0x%08x during CORE_RESUME\n", mbox0);
        }

        nv_falcon_set_os(dev, &gsp_flcn, gsp->boot_desc.appVersion);

        if (!nv_falcon_is_riscv_active(dev, &gsp_flcn)) {
          kprintf("[NV/GSP] Error: GSP RISC-V inactive after CORE_RESUME (STATUS=0x%08x)\n", nv_rd32(dev, NV_FALCON_GSP_RISCV_BASE + NV_PRISCV_RISCV_CORE_SWITCH_STATUS));
        }
        break;
      }
    }
  }
}

int nv_gsp_wait_init_done(const NvDevice *dev, const NvGspContext *gsp, uint32_t timeout_ms) {
  if (!dev || !gsp || !gsp->msgq_wptr || !gsp->msgq_rptr) {
    return -1;
  }

  uint32_t elapsed = 0;
  while (elapsed < timeout_ms) {
    uint32_t wptr = *gsp->msgq_wptr;
    uint32_t rptr = *gsp->msgq_rptr;

    if (wptr != rptr) {
      const uint8_t *msgq_base = (const uint8_t *)gsp->shm.virt_addr + GSP_SHM_MSGQ_OFFSET;
      const uint8_t *slot = msgq_base + GSP_PAGE_SIZE + (rptr * GSP_PAGE_SIZE);

      const NvGspRpcHdr *rpc = (const NvGspRpcHdr *)(slot + sizeof(NvGspMsgElemHdr));
      uint32_t fn_id = rpc->function;

      // kprintf("[NV/GSP] Received message from GSP-RM in msgq: fn=0x%04x (wptr=%u, rptr=%u)\n", fn_id, wptr, rptr);

      if (fn_id == NV_VGPU_MSG_EVENT_GSP_RUN_CPU_SEQUENCER) {
        turing_execute_cpu_sequencer(dev, gsp, (uint8_t *)slot);

        *gsp->msgq_rptr = (rptr + 1) % 63;
        nv_dma_mb();
        continue;
      }

      if (fn_id == NV_VGPU_MSG_EVENT_GSP_INIT_DONE) {
        kprintf("[NV/GSP] GSP OK\n");
        *gsp->msgq_rptr = (rptr + 1) % 63;
        nv_dma_mb();

        while (*gsp->msgq_rptr != *gsp->msgq_wptr) {
          uint32_t cur = *gsp->msgq_rptr;
          const uint8_t *s = msgq_base + GSP_PAGE_SIZE + (cur * GSP_PAGE_SIZE);
          const NvGspRpcHdr *r = (const NvGspRpcHdr *)(s + sizeof(NvGspMsgElemHdr));
          // kprintf("[NV/GSP] msgq slot %u: fn=0x%04x\n", cur, r->function);
          *gsp->msgq_rptr = (cur + 1) % 63;
          nv_dma_mb();
        }

        NvGspMsgqRxHeader *cmdq_rx = (NvGspMsgqRxHeader *)(gsp->shm.virt_addr + GSP_SHM_CMDQ_OFFSET + sizeof(NvGspMsgqTxHeader));
        nv_dma_clflush_range(cmdq_rx, sizeof(NvGspMsgqRxHeader));
        nv_wr32(dev, NV_FALCON_GSP_BASE + NV_FALCON_IRQSCLR, NV_FALCON_IRQSCLR_SWGEN0_SET);
        nv_dma_mb();
        return 0;
      }

      *gsp->msgq_rptr = (rptr + 1) % 63;
      nv_dma_mb();
    }

    hpet_sleep_us(1000);
    elapsed++;
  }

  kprintf("[NV/GSP] Error: Timeout waiting for GSP_INIT_DONE (wptr=%u, rptr=%u)\n", *gsp->msgq_wptr, *gsp->msgq_rptr);
  return -1;
}

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

  if (nv_dma_alloc(&dev->flush_page, 0x1000) != 0) {
    kprintf("[NV/TU] Error: Failed to allocate sysmem flush page\n");
    return -1;
  }

  uint32_t flush_lo = (uint32_t)(dev->flush_page.phys_addr >> 8);
  uint32_t flush_hi = (uint32_t)(dev->flush_page.phys_addr >> 40);

  nv_wr32(dev, NV_PFB_NISO_FLUSH_SYSMEM_ADDR, flush_lo);
  nv_wr32(dev, NV_PFB_NISO_FLUSH_SYSMEM_ADDR_HI, flush_hi);
  nv_dma_wmb();

  kprintf("[NV/TU] Sysmem Flush Page registered: PA=0x%016llx (LO=0x%08x, HI=0x%08x)\n", dev->flush_page.phys_addr, flush_lo, flush_hi);

  if (nv_bus_bind_bar1_phys(dev, 0x0, NV_PBUS_BAR1_BLOCK_TARGET_VID_MEM) != 0) {
    kprintf("[NV/TU] Error: Failed to bind BAR1 to physical VRAM\n");
    nv_dma_free(&dev->flush_page);
    return -1;
  }

  if (nv_vmm_create(&dev->vmm, NV_VMM_DEFAULT_VA_START, NV_VMM_DEFAULT_VA_LIMIT) != 0) {
    kprintf("[NV/TU] Error: Failed to create device VMM address space\n");
    nv_dma_free(&dev->flush_page);
    return -1;
  }

  nv_mmu_tlb_invalidate(dev, dev->vmm.pdb.phys_addr);

  //kprintf("[NV/TU] PDB: 0x%016llx\n", dev->vmm.pdb.phys_addr);

  nv_bios_verify_test(dev);

  NvGspContext gsp_ctx;
  if (nv_gsp_fw_stage_all(dev, &gsp_ctx) != 0) {
    kprintf("[NV/TU] Error: Firmware staging failed\n");
    nv_dma_free(&dev->flush_page);
    return -1;
  }

  nv_gsp_rpc_subsystem_init(&gsp_ctx);

  uint64_t fb_size = nv_fb_get_real_size(dev);
  if (nv_wpr_populate_meta(dev, &gsp_ctx, fb_size) != 0) {
    kprintf("[NV/TU] Error: WPR metadata population failed\n");
    nv_gsp_fw_cleanup(&gsp_ctx);
    nv_dma_free(&dev->flush_page);
    return -1;
  }

  NvBios bios;
  NvFwsecImage fwsec;
  if (nv_bios_init(dev, &bios) != 0 || nv_bios_extract_fwsec(&bios, &fwsec) != 0) {
    kprintf("[NV/TU] Error: Failed to extract FWSEC from VBIOS\n");
    nv_gsp_fw_cleanup(&gsp_ctx);
    nv_dma_free(&dev->flush_page);
    return -1;
  }

  const GspFwWprMeta *meta = (const GspFwWprMeta *)gsp_ctx.wpr_meta.virt_addr;
  if (nv_fwsec_execute_frts(dev, &fwsec, meta->frtsOffset) != 0) {
    kprintf("[NV/TU] Error: Failed to execute FWSEC-FRTS\n");
    nv_bios_free(&bios);
    nv_gsp_fw_cleanup(&gsp_ctx);
    nv_dma_free(&dev->flush_page);
    return -1;
  }
  nv_bios_free(&bios);

  NvFalcon gsp_flcn;
  nv_falcon_init_gsp(&gsp_flcn);

  if (nv_falcon_reset_into_riscv(dev, &gsp_flcn) != 0) {
    kprintf("[NV/TU] Error: Failed to switch GSP into RISC-V mode\n");
    nv_gsp_fw_cleanup(&gsp_ctx);
    nv_dma_free(&dev->flush_page);
    return -1;
  }

  uint32_t libos_lo = (uint32_t)(gsp_ctx.libos.phys_addr & 0xFFFFFFFFU);
  uint32_t libos_hi = (uint32_t)(gsp_ctx.libos.phys_addr >> 32);
  nv_falcon_mailbox_write(dev, &gsp_flcn, 0, libos_lo);
  nv_falcon_mailbox_write(dev, &gsp_flcn, 1, libos_hi);
  nv_falcon_set_os(dev, &gsp_flcn, gsp_ctx.boot_desc.appVersion);

  if (nv_gsp_submit_preinit_sequence(dev, &gsp_ctx) != 0) {
    kprintf("[NV/TU] Error: Failed to stage pre init RPCs into cmdq\n");
    nv_gsp_fw_cleanup(&gsp_ctx);
    nv_dma_free(&dev->flush_page);
    return -1;
  }

  NvSec2Context sec2_ctx;
  if (nv_sec2_init(dev, &sec2_ctx) != 0 || nv_sec2_stage_booter(&sec2_ctx) != 0 || nv_sec2_execute_booter_load(dev, &sec2_ctx, &gsp_ctx) != 0) {
    kprintf("[NV/TU] Error: SEC2 booter execution failed\n");
    nv_sec2_cleanup(&sec2_ctx);
    nv_gsp_fw_cleanup(&gsp_ctx);
    nv_dma_free(&dev->flush_page);
    return -1;
  }

  nv_sec2_cleanup(&sec2_ctx);
  if (nv_gsp_wait_init_done(dev, &gsp_ctx, 10000) != 0) {
    kprintf("[NV/TU] Error: GSP-RM initialization failed\n");
    nv_gsp_fw_cleanup(&gsp_ctx);
    nv_dma_free(&dev->flush_page);
    return -1;
  }

  GspStaticConfigInfo static_info;
  memset(&static_info, 0, sizeof(static_info));

  if (nv_gsp_get_static_info(dev, &gsp_ctx, &static_info) != 0) {
    kprintf("[NV/TU] Error: Failed to query GSP static configuration\n");
    nv_gsp_fw_cleanup(&gsp_ctx);
    nv_dma_free(&dev->flush_page);
    return -1;
  }

  if (nv_gsp_intr_get_table(dev, &gsp_ctx, &static_info) != 0) {
    kprintf("[NV/TU] Error: Failed to query hardware interrupt table\n");
    nv_gsp_fw_cleanup(&gsp_ctx);
    nv_dma_free(&dev->flush_page);
    return -1;
  }

  if (turing_rm_objects_init(dev, &gsp_ctx, &static_info) != 0) {
    kprintf("[NV/TU] Error: RM Object hierarchy initialization failed\n");
    nv_gsp_fw_cleanup(&gsp_ctx);
    nv_dma_free(&dev->flush_page);
    return -1;
  }


  return 0;

}

static void turing_cleanup(NvDevice *dev) {
  kprintf("[NV/TU] Shutting down %s\n", dev->chip.chip_name);

  if (dev->flush_page.phys_addr) {
    nv_dma_free(&dev->flush_page);
  }

  if (dev->vmm.pdb.phys_addr) {
    nv_mmu_tlb_invalidate(dev, dev->vmm.pdb.phys_addr);
    nv_vmm_destroy(&dev->vmm);
  }
}


const NvArchOps g_turing_ops = {
  .init = turing_init,
  .cleanup = turing_cleanup,
};
