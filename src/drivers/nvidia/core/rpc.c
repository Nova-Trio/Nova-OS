#include <nv_rpc.h>
#include <nv_device.h>
#include <nv_dma.h>
#include <nv_reg.h>
#include <nv_falcon.h>
#include <novamod.h>
#include <stdint.h>

#define ALIGN_UP(x, a) (((x) + ((a) - 1ULL)) & ~((a) - 1ULL))
#define MIN(a, b) (((a) < (b)) ? (a) : (b))

static NvGspMsgNtfy g_ntfy_table[NV_GSP_MAX_MSG_NTFY];
static uint32_t g_ntfy_count = 0;


int nv_rpc_status_to_errno(uint32_t rpc_status) {
  switch (rpc_status) {
    case 0x00: // NV_OK
      return 0;
    case 0x51: // NV_ERR_NO_MEMORY
      return -12; // -ENOMEM
    case 0x55: // NV_ERR_NOT_READY
    case 0x66: // NV_ERR_TIMEOUT_RETRY
      return -16; // -EBUSY / -EAGAIN
    case 0x56: // NV_ERR_NOT_SUPPORTED
      return -95; // -EOPNOTSUPP
    default:
      return -22; // -EINVAL
  }
}

static void rpc_dump_libos_log(const NvDmaBuffer *buf, const char *name) {
  if (!buf || !buf->virt_addr) return;
  const uint8_t *raw = (const uint8_t *)buf->virt_addr;
  uint64_t put_ptr = *(const uint64_t *)raw;

  kprintf("\n[NV/LOG] START LibOS %s Log Buffer (Put: 0x%llx)\n", name, put_ptr);
  if (put_ptr <= 136) {
    kprintf("[NV/LOG] (No payload entries written beyond PTE table)\n");
    kprintf("[NV/LOG] END\n\n");
    return;
  }

  uint64_t len = put_ptr - 136;
  const uint8_t *payload = raw + 136;
  char ascii[17];
  ascii[16] = '\0';

  for (uint64_t i = 0; i < len; i += 16) {
    kprintf("  %04x: ", (uint32_t)i);
    for (uint64_t j = 0; j < 16; j++) {
      if (i + j < len) {
        uint8_t byte = payload[i + j];
        kprintf("%02x ", byte);
        ascii[j] = (byte >= 32 && byte <= 126) ? byte : '.';
      } else {
        kprintf("   ");
        ascii[j] = ' ';
      }
    }
    kprintf(" |%s|\n", ascii);
  }
  kprintf("[NV/LOG] END\n\n");
}

static int rpc_default_error_log_handler(void *priv, uint32_t fn, const void *repv, uint32_t repc) {
  (void)priv;
  (void)fn;
  if (!repv || repc < sizeof(rpc_os_error_log_v17_00)) {
    return -1;
  }
  const rpc_os_error_log_v17_00 *log = (const rpc_os_error_log_v17_00 *)repv;
  kprintf("[NV/GSP/XID] Error Log: exceptType=%u, runlistId=%u, chid=%u: %s\n", log->exceptType, log->runlistId, log->chid, log->errString);
  return 0;
}

static int rpc_default_user_shared_data_handler(void *priv, uint32_t fn, const void *repv, uint32_t repc) {
  (void)priv;
  (void)fn;
  (void)repv;
  (void)repc;
  return 0;
}

void nv_gsp_rpc_subsystem_init(NvGspContext *gsp) {
  (void)gsp;
  memset(g_ntfy_table, 0, sizeof(g_ntfy_table));
  g_ntfy_count = 0;

  nv_gsp_msg_ntfy_add(gsp, NV_VGPU_MSG_EVENT_OS_ERROR_LOG, rpc_default_error_log_handler, NULL);
  nv_gsp_msg_ntfy_add(gsp, NV_VGPU_MSG_EVENT_GSP_SEND_USER_SHARED_DATA, rpc_default_user_shared_data_handler, NULL);
}

int nv_gsp_msg_ntfy_add(NvGspContext *gsp, uint32_t fn, NvGspMsgNtfyFunc func, void *priv) {
  (void)gsp;
  for (uint32_t i = 0; i < g_ntfy_count; i++) {
    if (g_ntfy_table[i].fn == fn) {
      g_ntfy_table[i].func = func;
      g_ntfy_table[i].priv = priv;
      return 0;
    }
  }

  if (g_ntfy_count >= NV_GSP_MAX_MSG_NTFY) {
    kprintf("[NV/RPC] Error: Notification table full\n");
    return -1;
  }

  g_ntfy_table[g_ntfy_count].fn = fn;
  g_ntfy_table[g_ntfy_count].func = func;
  g_ntfy_table[g_ntfy_count].priv = priv;
  g_ntfy_count++;
  return 0;
}

void *nv_gsp_rpc_get(const NvGspContext *gsp, uint32_t fn, uint32_t payload_size) {
  if (!gsp || !gsp->cmdq_wptr) {
    return NULL;
  }

  uint32_t cur_wptr = *gsp->cmdq_wptr;
  uint32_t rpc_len = (uint32_t)sizeof(NvGspRpcHdr) + payload_size;
  size_t total_size = sizeof(NvGspMsgElemHdr) + rpc_len;
  size_t aligned_size = ALIGN_UP(total_size, GSP_PAGE_SIZE);

  uint8_t *slot = (uint8_t *)gsp->shm.virt_addr + GSP_SHM_CMDQ_OFFSET + GSP_PAGE_SIZE + (cur_wptr * GSP_PAGE_SIZE);
  memset(slot, 0, aligned_size);

  NvGspMsgElemHdr *elem = (NvGspMsgElemHdr *)slot;
  NvGspRpcHdr *rpc = (NvGspRpcHdr *)(slot + sizeof(NvGspMsgElemHdr));

  elem->pad = 0;
  elem->checksum = 0;
  elem->elem_count = (uint32_t)(aligned_size / GSP_PAGE_SIZE);

  rpc->header_version = GSP_RPC_HEADER_VERSION;
  rpc->signature = GSP_RPC_SIGNATURE;
  rpc->length = rpc_len;
  rpc->function = fn;
  rpc->rpc_result = 0xFFFFFFFFU;
  rpc->rpc_result_private = 0xFFFFFFFFU;
  rpc->sequence = 0;
  rpc->cpu_rm_gfid = 0;

  return (void *)(slot + sizeof(NvGspMsgElemHdr) + sizeof(NvGspRpcHdr));
}

void nv_gsp_rpc_done(const NvGspContext *gsp, void *repv) {
  (void)gsp;
  if (repv) {
    kfree(repv);
  }
}

static int nv_gsp_cmdq_push_inplace(const NvDevice *dev, NvGspContext *gsp, void *payload_ptr) {
  if (!dev || !gsp || !gsp->cmdq_wptr || !payload_ptr) {
    return -1;
  }

  NvGspMsgElemHdr *elem = to_gsp_msg_hdr(payload_ptr);
  NvGspRpcHdr *rpc = to_gsp_rpc_hdr(payload_ptr);

  uint32_t cur_wptr = *gsp->cmdq_wptr;
  uint32_t elem_count = elem->elem_count;
  size_t aligned_size = elem_count * GSP_PAGE_SIZE;

  elem->pad = 0;
  elem->checksum = 0;
  elem->sequence = gsp->cmdq_seq++;
  rpc->sequence = 0;

  uint64_t csum = 0;
  const uint64_t *ptr = (const uint64_t *)elem;
  const uint64_t *end = (const uint64_t *)((const uint8_t *)elem + aligned_size);
  while (ptr < end) {
    csum ^= *ptr++;
  }
  elem->checksum = (uint32_t)(csum >> 32) ^ (uint32_t)(csum & 0xFFFFFFFFU);

  //kprintf("[NV/RPC] Slot %u Header: csum=0x%08x, seq=%u, fn=0x%02x, len=%u, wptr=%u\n",cur_wptr, elem->checksum, elem->sequence, rpc->function, rpc->length, cur_wptr);

  uint32_t next_wptr = (cur_wptr + elem_count) % 63U;
  *gsp->cmdq_wptr = next_wptr;

  nv_dma_clflush_range(elem, aligned_size);
  NvGspMsgqTxHeader *cmdq_tx = (NvGspMsgqTxHeader *)(gsp->shm.virt_addr + GSP_SHM_CMDQ_OFFSET);
  nv_dma_clflush_range(cmdq_tx, sizeof(NvGspMsgqTxHeader));

  nv_dma_wmb();
  nv_dma_mb();

  nv_wr32(dev, NV_FALCON_GSP_BASE + NV_FALCON_IRQMSET, 0x00000040U);
  nv_wr32(dev, NV_FALCON_GSP_BASE + NV_FALCON_IRQSCLR, NV_FALCON_IRQSCLR_SWGEN0_SET);

  *(volatile uint8_t *)((uint8_t *)dev->bar0.virt_addr + NV_FALCON_GSP_BASE + 0x00000C00U) = 0;

  nv_wr32(dev, NV_FALCON_GSP_BASE + 0x00000C00U, 0x00000000U);
  (void)nv_rd32(dev, NV_FALCON_GSP_BASE + 0x00000C00U);
  nv_dma_mb();
  return 0;
}

void *nv_gsp_msg_recv(const NvDevice *dev, NvGspContext *gsp, uint32_t fn, uint32_t gsp_rpc_len) {
  if (!dev || !gsp || !gsp->msgq_wptr || !gsp->msgq_rptr) {
    return NULL;
  }

  uint32_t timeout_us = NV_GSP_RPC_DEFAULT_TIMEOUT_US;
  NvGspMsgqTxHeader *msgq_tx = (NvGspMsgqTxHeader *)(gsp->shm.virt_addr + GSP_SHM_MSGQ_OFFSET);
  NvGspMsgqRxHeader *cmdq_rx = (NvGspMsgqRxHeader *)(gsp->shm.virt_addr + GSP_SHM_CMDQ_OFFSET + sizeof(NvGspMsgqTxHeader));

  while (timeout_us > 0) {
    if ((timeout_us % 100000) == 0 && (*gsp->cmdq_rptr != *gsp->cmdq_wptr)) {
      *(volatile uint8_t *)((uint8_t *)dev->bar0.virt_addr + NV_FALCON_GSP_BASE + 0x00000C00U) = 0;
      nv_wr32(dev, NV_FALCON_GSP_BASE + 0x00000C00U, 0x00000000U);
      (void)nv_rd32(dev, NV_FALCON_GSP_BASE + 0x00000C00U);
      nv_dma_mb();
    }

    nv_dma_clflush_range(msgq_tx, sizeof(NvGspMsgqTxHeader));
    nv_dma_mb();

    uint32_t wptr = *gsp->msgq_wptr;
    uint32_t rptr = *gsp->msgq_rptr;

    if (wptr != rptr) {
      const uint8_t *msgq_base = (const uint8_t *)gsp->shm.virt_addr + GSP_SHM_MSGQ_OFFSET + GSP_PAGE_SIZE;
      const uint8_t *slot = msgq_base + (rptr * GSP_PAGE_SIZE);

      nv_dma_clflush_range(slot, GSP_PAGE_SIZE);
      nv_dma_mb();

      const NvGspMsgElemHdr *rx_elem = (const NvGspMsgElemHdr *)slot;
      const NvGspRpcHdr *rx_rpc = (const NvGspRpcHdr *)(slot + sizeof(NvGspMsgElemHdr));

      uint32_t rx_fn = rx_rpc->function;
      uint32_t rx_elem_count = rx_elem->elem_count ? rx_elem->elem_count : 1;

      //kprintf("[NV/RPC] Received msgq slot %u: fn=0x%04x, seq=%u, elem_count=%u (wptr=%u, rptr=%u)\n", rptr, rx_fn, rx_rpc->sequence, rx_elem_count, wptr, rptr);

      // Acknowledge SWGEN0 interrupt
      nv_wr32(dev, NV_FALCON_GSP_BASE + NV_FALCON_IRQSCLR, NV_FALCON_IRQSCLR_SWGEN0_SET);

      if (rx_fn >= 0x1000U) {
        for (uint32_t i = 0; i < g_ntfy_count; i++) {
          if (g_ntfy_table[i].fn == rx_fn) {
            if (g_ntfy_table[i].func) {
              const void *ev_payload = (const void *)(slot + sizeof(NvGspMsgElemHdr) + sizeof(NvGspRpcHdr));
              uint32_t ev_len = (rx_rpc->length > sizeof(NvGspRpcHdr)) ?
              (rx_rpc->length - (uint32_t)sizeof(NvGspRpcHdr)) : 0;
              g_ntfy_table[i].func(g_ntfy_table[i].priv, rx_fn, ev_payload, ev_len);
            }
            break;
          }
        }

        *gsp->msgq_rptr = (rptr + rx_elem_count) % 63U;
        nv_dma_clflush_range(cmdq_rx, sizeof(NvGspMsgqRxHeader));
        nv_dma_mb();
        continue;
      }

      if (fn != 0 && rx_fn == fn) {
        if (rx_rpc->rpc_result != 0) {
          kprintf("[NV/RPC] Error: RPC fn=0x%x returned error 0x%08x (private=0x%08x)\n",
                  rx_fn, rx_rpc->rpc_result, rx_rpc->rpc_result_private);
          *gsp->msgq_rptr = (rptr + rx_elem_count) % 63U;
          nv_dma_clflush_range(cmdq_rx, sizeof(NvGspMsgqRxHeader));
          nv_dma_mb();
          return NULL;
        }

        uint32_t payload_len = rx_rpc->length - (uint32_t)sizeof(NvGspRpcHdr);
        uint32_t alloc_len = (gsp_rpc_len > 0) ? gsp_rpc_len : payload_len;

        void *reply_buf = kzalloc(alloc_len);
        if (!reply_buf) {
          kprintf("[NV/RPC] Error: Failed to allocate %u bytes for RPC reply\n", alloc_len);
          *gsp->msgq_rptr = (rptr + rx_elem_count) % 63U;
          nv_dma_clflush_range(cmdq_rx, sizeof(NvGspMsgqRxHeader));
          nv_dma_mb();
          return NULL;
        }

        size_t bytes_to_copy = MIN(payload_len, alloc_len);
        memcpy(reply_buf, slot + sizeof(NvGspMsgElemHdr) + sizeof(NvGspRpcHdr), bytes_to_copy);

        *gsp->msgq_rptr = (rptr + rx_elem_count) % 63U;
        nv_dma_clflush_range(cmdq_rx, sizeof(NvGspMsgqRxHeader));
        nv_dma_mb();
        return reply_buf;
      }

      *gsp->msgq_rptr = (rptr + rx_elem_count) % 63U;
      nv_dma_clflush_range(cmdq_rx, sizeof(NvGspMsgqRxHeader));
      nv_dma_mb();
    }

    hpet_sleep_us(50);
    if (timeout_us >= 50) timeout_us -= 50; else break;
  }

  kprintf("[NV/RPC] Error: Timeout waiting for RPC response fn=0x%x (wptr=%u, rptr=%u, GSP_cmdq_rptr=%u)\n", fn, *gsp->msgq_wptr, *gsp->msgq_rptr, *gsp->cmdq_rptr);

  uint32_t cpuctl = nv_rd32(dev, NV_FALCON_GSP_BASE + NV_FALCON_CPUCTL);
  uint32_t riscv_status = nv_rd32(dev, NV_FALCON_GSP_RISCV_BASE + NV_PRISCV_RISCV_CORE_SWITCH_STATUS);
  uint32_t irqstat = nv_rd32(dev, NV_FALCON_GSP_BASE + NV_FALCON_IRQSTAT);
  uint32_t irqmask = nv_rd32(dev, NV_FALCON_GSP_BASE + NV_FALCON_IRQMASK);
  uint32_t mbox0 = nv_rd32(dev, NV_FALCON_GSP_BASE + NV_FALCON_MAILBOX0);
  uint32_t mbox1 = nv_rd32(dev, NV_FALCON_GSP_BASE + NV_FALCON_MAILBOX1);

  kprintf("[NV/RPC] GSP HW Status: CPUCTL=0x%08x, RISCV_STATUS=0x%08x, IRQSTAT=0x%08x, IRQMASK=0x%08x, MBOX0=0x%08x, MBOX1=0x%08x\n", cpuctl, riscv_status, irqstat, irqmask, mbox0, mbox1);

  rpc_dump_libos_log(&gsp->logrm, "LOGRM");
  rpc_dump_libos_log(&gsp->loginit, "LOGINIT");
  return NULL;
}

void *nv_gsp_rpc_push(const NvDevice *dev, NvGspContext *gsp, void *payload,
                      NvGspRpcReplyPolicy policy, uint32_t expected_reply_len) {
  if (!dev || !gsp || !payload) {
    return NULL;
  }

  NvGspRpcHdr *rpc = to_gsp_rpc_hdr(payload);
  uint32_t fn = rpc->function;

  if (nv_gsp_cmdq_push_inplace(dev, gsp, payload) != 0) {
    return NULL;
  }

  switch (policy) {
    case NV_GSP_RPC_REPLY_NOWAIT:
    case NV_GSP_RPC_REPLY_NOSEQ:
      return NULL;
    case NV_GSP_RPC_REPLY_RECV:
      return nv_gsp_msg_recv(dev, gsp, fn, expected_reply_len);
    case NV_GSP_RPC_REPLY_POLL:
      return nv_gsp_msg_recv(dev, gsp, fn, 0);
    default:
      return NULL;
  }
}

int nv_gsp_rpc_poll(const NvDevice *dev, NvGspContext *gsp, uint32_t fn) {
  void *reply = nv_gsp_msg_recv(dev, gsp, fn, 0);
  if (!reply && fn != NV_VGPU_MSG_EVENT_GSP_INIT_DONE) {
    return -1;
  }
  if (reply) {
    nv_gsp_rpc_done(gsp, reply);
  }
  return 0;
}

int nv_gsp_get_static_info(const NvDevice *dev, NvGspContext *gsp, GspStaticConfigInfo *out_info) {
  if (!dev || !gsp || !out_info) {
    return -1;
  }

  void *payload = nv_gsp_rpc_get(gsp, NV_VGPU_MSG_FUNCTION_GET_GSP_STATIC_INFO, (uint32_t)sizeof(GspStaticConfigInfo));
  if (!payload) {
    return -1;
  }

  GspStaticConfigInfo *reply = (GspStaticConfigInfo *)nv_gsp_rpc_push(
    dev, gsp, payload, NV_GSP_RPC_REPLY_RECV, (uint32_t)sizeof(GspStaticConfigInfo));

  if (!reply) {
    kprintf("[NV/GSP] Error: GET_GSP_STATIC_INFO RPC failed\n");
    return -1;
  }

  memcpy(out_info, reply, sizeof(GspStaticConfigInfo));

  /*
  const uint8_t *raw = (const uint8_t *)reply;
  kprintf("\n[NV/GSP] Static Telemetry Buffer (0x0000 - 0x0500):\n");
  for (uint32_t i = 0; i < 1280; i += 16) {
    kprintf("  %04x: ", i);
    char ascii[17];
    ascii[16] = '\0';
    for (uint32_t j = 0; j < 16; j++) {
      uint8_t b = raw[i + j];
      kprintf("%02x ", b);
      ascii[j] = (b >= 32 && b <= 126) ? b : '.';
    }
    kprintf(" |%s|\n", ascii);
  }
  */

  nv_gsp_rpc_done(gsp, reply);
  nv_gsp_dump_static_info(out_info);
  return 0;
}

void nv_gsp_dump_static_info(const GspStaticConfigInfo *info) {
  if (!info) {
    return;
  }

  kprintf("\nGPU STATIC INFO\n");
  kprintf("GPU Name String      : %s\n", (const char *)info->gpuNameString);
  kprintf("GPU Short Name       : %s\n", (const char *)info->gpuShortNameString);
  kprintf("Internal Client ID   : 0x%08x\n", info->hInternalClient);
  kprintf("Internal Device ID   : 0x%08x\n", info->hInternalDevice);
  kprintf("Internal Subdevice ID: 0x%08x\n", info->hInternalSubdevice);
  kprintf("BAR1 Page Dir Base   : 0x%016llx\n", info->bar1PdeBase);
  kprintf("BAR2 Page Dir Base   : 0x%016llx\n", info->bar2PdeBase);
  kprintf("Total Usable VRAM    : %llu MB (0x%016llx)\n", info->fb_length / (1024ULL * 1024ULL), info->fb_length);
  kprintf("VRAM Bus Width       : %u-bit\n", info->fb_bus_width);
  kprintf("VRAM RAM Type        : 0x%02x (GDDR%u)\n", info->fb_ram_type, (info->fb_ram_type == 2 ? 5 : 6));
  kprintf("L2 Cache Size        : %u KB\n", info->l2_cache_size / 1024U);
  kprintf("VBIOS SKU Chip       : %s (BoardID: 0x%04x)\n", info->SKUInfo.chipSKU, info->SKUInfo.BoardID);
  kprintf("VBIOS SKU Project    : %s (SKU: %s)\n", info->SKUInfo.project, info->SKUInfo.projectSKU);
  kprintf("FB Regions Count     : %u regions\n", info->fbRegionInfoParams.numFBRegions);
  for (uint32_t i = 0; i < info->fbRegionInfoParams.numFBRegions && i < 4; i++) {
    kprintf("    Region %u         : 0x%016llx - 0x%016llx (prot=%u)\n", i, info->fbRegionInfoParams.fbRegion[i].base, info->fbRegionInfoParams.fbRegion[i].limit,
            info->fbRegionInfoParams.fbRegion[i].bProtected);
  }
  kprintf("**END**\n\n");
}


int nv_gsp_intr_get_table(const NvDevice *dev, NvGspContext *gsp, const GspStaticConfigInfo *static_info) {
  if(!dev || !gsp || !static_info){
    return -1;
  }

  uint32_t params_size = (uint32_t)sizeof(NV2080_CTRL_INTERNAL_INTR_GET_KERNEL_TABLE_PARAMS);
  uint32_t alloc_size = (uint32_t)sizeof(rpc_gsp_rm_control_v03_00) + params_size;

  void *payload = nv_gsp_rpc_get(gsp, NV_VGPU_MSG_FUNCTION_GSP_RM_CONTROL, alloc_size);

  if (!payload) {
    kprintf("[NV/GSP] Error: Failed to allocate RPC buffer for INTR_GET_KERNEL_TABLE\n");
    return -1;
  }

  rpc_gsp_rm_control_v03_00 *ctrl = (rpc_gsp_rm_control_v03_00 *)payload;
  ctrl->hClient = static_info->hInternalClient;
  ctrl->hObject = static_info->hInternalSubdevice;
  ctrl->cmd = NV2080_CTRL_CMD_INTERNAL_INTR_GET_KERNEL_TABLE;
  ctrl->status = 0;
  ctrl->paramsSize = params_size;
  ctrl->flags = 0;

  NV2080_CTRL_INTERNAL_INTR_GET_KERNEL_TABLE_PARAMS *params = (NV2080_CTRL_INTERNAL_INTR_GET_KERNEL_TABLE_PARAMS *)ctrl->params;
  memset(params, 0, sizeof(*params));

  rpc_gsp_rm_control_v03_00 *reply = (rpc_gsp_rm_control_v03_00 *)nv_gsp_rpc_push(dev, gsp, payload, NV_GSP_RPC_REPLY_RECV, alloc_size);

  if (!reply) {
    kprintf("[NV/GSP] Error: INTR_GET_KERNEL_TABLE RPC failed\n");
    return -1;
  }

  if (reply->status != 0) {
    uint32_t err_status = reply->status;
    kprintf("[NV/GSP] Error: INTR_GET_KERNEL_TABLE returned error 0x%08x\n", err_status);
    nv_gsp_rpc_done(gsp, reply);
    return nv_rpc_status_to_errno(err_status);
  }

  NV2080_CTRL_INTERNAL_INTR_GET_KERNEL_TABLE_PARAMS *res_params = (NV2080_CTRL_INTERNAL_INTR_GET_KERNEL_TABLE_PARAMS *)reply->params;

  kprintf("\nInterrupt Routings\n");
  kprintf("Discovered Entries: %u\n", res_params->tableLen);
  for (uint32_t i = 0; i < res_params->tableLen && i < NV2080_CTRL_INTERNAL_INTR_MAX_TABLE_SIZE; i++) {
    kprintf("[%02u] EngineIdx: 0x%04x | PMC Mask: 0x%08x | Stall: 0x%04x | Non-Stall: 0x%04x\n", i, res_params->table[i].engineIdx, res_params->table[i].pmcIntrMask,
            res_params->table[i].vectorStall,
            res_params->table[i].vectorNonStall);
  }
  kprintf("**END**\n\n");

  nv_gsp_rpc_done(gsp, reply);
  return 0;
}
