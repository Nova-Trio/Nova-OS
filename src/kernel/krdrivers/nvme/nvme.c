#include "nvme.h"
#include <vmm.h>
#include <heap.h>
#include <console.h>
#include <hpet.h>
#include <pmm.h>
#include <string.h>
#include <lapic.h>
#include <idt.h>

static NvmeController *g_controllers = NULL;

static int nvme_wait_csts(NvmeController *ctrl, uint32_t mask, uint32_t expected) {
  uint64_t start = hpet_get_millis();
  while ((ctrl->regs->csts & mask) != expected) {
    if (ctrl->regs->csts & NVME_CSTS_CFS) {
      kprintf("[NVMe] Fatal: Controller reports fatal status\n");
      return -1;
    }
    if ((hpet_get_millis() - start) >= ctrl->timeout_ms) {
      kprintf("[NVMe] Error: Timeout waiting for CSTS (mask 0x%x, expected 0x%x)\n", mask, expected);
      return -1;
    }
    __asm__ volatile("pause");
  }
  return 0;
}


static int nvme_reset_controller(NvmeController *ctrl) {
  if (ctrl->regs->cc & NVME_CC_EN) {
    ctrl->regs->cc &= ~NVME_CC_EN;
  }
  return nvme_wait_csts(ctrl, NVME_CSTS_RDY, 0);
}

static int nvme_setup_admin_queue(NvmeController *ctrl) {
  uint16_t q_entries = NVME_ADMIN_QUEUE_ENTRIES;
  if (q_entries > ctrl->max_queue_entries) {
    q_entries = (uint16_t)ctrl->max_queue_entries;
  }

  void *sq_frame = pmm_alloc_frame();
  void *cq_frame = pmm_alloc_frame();
  if (!sq_frame || !cq_frame) {
    if (sq_frame) pmm_free_frame(sq_frame);
    if (cq_frame) pmm_free_frame(cq_frame);
    return -1;
  }

  ctrl->admin_queue.sq_phys = (uint64_t)sq_frame;
  ctrl->admin_queue.cq_phys = (uint64_t)cq_frame;
  ctrl->admin_queue.sq = (NvmeSqe *)(ctrl->admin_queue.sq_phys + HHDM_BASE);
  ctrl->admin_queue.cq = (NvmeCqe *)(ctrl->admin_queue.cq_phys + HHDM_BASE);

  memset(ctrl->admin_queue.sq, 0, PAGE_SIZE);
  memset(ctrl->admin_queue.cq, 0, PAGE_SIZE);

  ctrl->admin_queue.size = q_entries;
  ctrl->admin_queue.sq_tail = 0;
  ctrl->admin_queue.cq_head = 0;
  ctrl->admin_queue.cq_phase = 1;

  uint32_t stride = 4 << ctrl->dstrd;
  uint8_t *mmio = (uint8_t *)ctrl->regs;
  ctrl->admin_queue.sq_doorbell = (volatile uint32_t *)(mmio + 0x1000 + (0 * stride));
  ctrl->admin_queue.cq_doorbell = (volatile uint32_t *)(mmio + 0x1000 + (1 * stride));

  ctrl->regs->aqa = ((uint32_t)(q_entries - 1) << 16) | (uint32_t)(q_entries - 1);
  ctrl->regs->asq = ctrl->admin_queue.sq_phys;
  ctrl->regs->acq = ctrl->admin_queue.cq_phys;

  return 0;
}

static int nvme_enable_controller(NvmeController *ctrl) {
  uint32_t cc = NVME_CC_EN | NVME_CC_CSS_NVM | NVME_CC_MPS_4K |
  NVME_CC_AMS_RR | NVME_CC_IOSQES_64 | NVME_CC_IOCQES_16;

  ctrl->regs->cc = cc;
  return nvme_wait_csts(ctrl, NVME_CSTS_RDY, NVME_CSTS_RDY);
}

static int nvme_submit_admin_cmd(NvmeController *ctrl, NvmeSqe *cmd, NvmeCqe *cqe_out) {
  NvmeQueue *q = &ctrl->admin_queue;
  uint16_t cid = q->sq_tail;
  cmd->cdw0 = (cmd->cdw0 & 0xFFFF) | ((uint32_t)cid << 16);

  q->sq[q->sq_tail] = *cmd;
  q->sq_tail = (q->sq_tail + 1) % q->size;

  __asm__ volatile("sfence" ::: "memory");
  *q->sq_doorbell = q->sq_tail;

  uint64_t start = hpet_get_millis();
  volatile NvmeCqe *cqe = &q->cq[q->cq_head];

  while ((cqe->status & 1) != q->cq_phase) {
    if (ctrl->regs->csts & NVME_CSTS_CFS) {
      kprintf("[NVMe] Fatal: Controller reports fatal status\n");
      return -1;
    }
    if ((hpet_get_millis() - start) >= ctrl->timeout_ms) {
      kprintf("[NVMe] Error: Admin command timeout (CID %u)\n", cid);
      return -1;
    }
    __asm__ volatile("pause");
  }

  if (cqe_out) {
    *cqe_out = *cqe;
  }

  uint16_t status = cqe->status >> 1;
  q->cq_head = (q->cq_head + 1) % q->size;
  if (q->cq_head == 0) {
    q->cq_phase ^= 1;
  }
  *q->cq_doorbell = q->cq_head;

  if (status != 0) {
    kprintf("[NVMe] Error: Admin command failed with status 0x%04x\n", status);
    return -1;
  }

  return 0;
}

static void trim_spaces(char *str, size_t len) {
  while (len > 0 && (str[len - 1] == ' ' || str[len - 1] == '\0')) {
    str[len - 1] = '\0';
    len--;
  }
}

static void nvme_probe_namespace(NvmeController *ctrl, uint32_t nsid, void *buf, uint64_t buf_phys) {
  memset(buf, 0, PAGE_SIZE);

  NvmeSqe cmd;
  memset(&cmd, 0, sizeof(cmd));
  cmd.cdw0 = NVME_ADMIN_OP_IDENTIFY;
  cmd.nsid = nsid;
  cmd.dptr[0] = buf_phys;
  cmd.cdw10 = 0;

  if (nvme_submit_admin_cmd(ctrl, &cmd, NULL) != 0) {
    return;
  }

  NvmeIdentifyNamespace *id_ns = (NvmeIdentifyNamespace *)buf;
  if (id_ns->nsze == 0) {
    return;
  }

  uint8_t flbas_idx = id_ns->flbas & 0xF;
  uint8_t lba_ds = id_ns->lbaf[flbas_idx].ds;
  if (lba_ds == 0) {
    return;
  }

  uint32_t block_size = 1U << lba_ds;
  uint64_t total_size_mb = (id_ns->nsze * block_size) / (1024 * 1024);

  NvmeNamespace *ns = (NvmeNamespace *)kmalloc(sizeof(NvmeNamespace));
  if (!ns) {
    return;
  }

  ns->nsid = nsid;
  ns->block_count = id_ns->nsze;
  ns->block_size = block_size;
  ns->next = ctrl->namespaces;
  ctrl->namespaces = ns;

  kprintf("[NVMe]  Namespace %u: %llu blocks (%u bytes)\n",
          nsid, ns->block_count, ns->block_size, total_size_mb);
}

static int nvme_identify(NvmeController *ctrl) {
  void *frame = pmm_alloc_frame();
  if (!frame) {
    return -1;
  }

  uint64_t buf_phys = (uint64_t)frame;
  uint8_t *buf = (uint8_t *)(buf_phys + HHDM_BASE);

  memset(buf, 0, PAGE_SIZE);
  NvmeSqe cmd;
  memset(&cmd, 0, sizeof(cmd));
  cmd.cdw0 = NVME_ADMIN_OP_IDENTIFY;
  cmd.nsid = 0;
  cmd.dptr[0] = buf_phys;
  cmd.cdw10 = 1;

  if (nvme_submit_admin_cmd(ctrl, &cmd, NULL) != 0) {
    pmm_free_frame(frame);
    return -1;
  }

  NvmeIdentifyController *id_ctrl = (NvmeIdentifyController *)buf;
  char model[41];
  char serial[21];
  memcpy(model, id_ctrl->mn, 40);
  model[40] = '\0';
  memcpy(serial, id_ctrl->sn, 20);
  serial[20] = '\0';
  trim_spaces(model, 40);
  trim_spaces(serial, 20);

  uint32_t nn = id_ctrl->nn;
  ctrl->max_transfer_bytes = (id_ctrl->mdts > 0) ? (PAGE_SIZE << id_ctrl->mdts) : 0;

  kprintf("[NVMe] Model: \"%s\" | S/N: \"%s\" | Namespaces: %u\n", model, serial, nn);

  ctrl->namespaces = NULL;

  if (ctrl->vs >= 0x00010100) {
    uint32_t last_nsid = 0;
    int done = 0;

    while (!done) {
      memset(buf, 0, PAGE_SIZE);
      memset(&cmd, 0, sizeof(cmd));
      cmd.cdw0 = NVME_ADMIN_OP_IDENTIFY;
      cmd.nsid = last_nsid;
      cmd.dptr[0] = buf_phys;
      cmd.cdw10 = 2;

      if (nvme_submit_admin_cmd(ctrl, &cmd, NULL) != 0) {
        break;
      }

      uint32_t *ns_list = (uint32_t *)buf;
      if (ns_list[0] == 0) {
        break;
      }

      for (size_t i = 0; i < 1024; i++) {
        uint32_t nsid = ns_list[i];
        if (nsid == 0) {
          done = 1;
          break;
        }

        void *ns_frame = pmm_alloc_frame();
        if (ns_frame) {
          nvme_probe_namespace(ctrl, nsid, (uint8_t *)((uint64_t)ns_frame + HHDM_BASE), (uint64_t)ns_frame);
          pmm_free_frame(ns_frame);
        }

        last_nsid = nsid;
        if (last_nsid >= nn) {
          done = 1;
          break;
        }
      }
    }
  }

  if (!ctrl->namespaces && nn > 0) {
    for (uint32_t nsid = 1; nsid <= nn; nsid++) {
      nvme_probe_namespace(ctrl, nsid, buf, buf_phys);
    }
  }

  pmm_free_frame(frame);
  return 0;
}

static void nvme_irq_handler(Registers *regs) {
  (void)regs;

  for (NvmeController *ctrl = g_controllers; ctrl; ctrl = ctrl->next) {
    if (!ctrl->use_msix) {
      continue;
    }

    NvmeQueue *q = &ctrl->io_queue;
    if (!q->cq || !q->contexts) {
      continue;
    }

    uint16_t processed = 0;
    while ((q->cq[q->cq_head].status & 1) == q->cq_phase) {
      NvmeCqe cqe = q->cq[q->cq_head];
      uint16_t cid = cqe.command_id;

      if (cid < q->size) {
        q->contexts[cid].cqe = cqe;
        q->contexts[cid].status = cqe.status >> 1;
        q->contexts[cid].done = 1;
      }

      q->cq_head = (q->cq_head + 1) % q->size;
      if (q->cq_head == 0) {
        q->cq_phase ^= 1;
      }
      processed++;
    }

    if (processed > 0) {
      *q->cq_doorbell = q->cq_head;
    }
  }

  lapic_eoi();
}

static int nvme_submit_io_cmd(NvmeController *ctrl, NvmeSqe *cmd, NvmeCqe *cqe_out) {
  NvmeQueue *q = &ctrl->io_queue;
  uint16_t cid = q->sq_tail;
  cmd->cdw0 = (cmd->cdw0 & 0xFFFF) | ((uint32_t)cid << 16);

  if (q->contexts) {
    q->contexts[cid].done = 0;
    q->contexts[cid].status = 0;
  }

  q->sq[q->sq_tail] = *cmd;
  q->sq_tail = (q->sq_tail + 1) % q->size;

  __asm__ volatile("sfence" ::: "memory");
  *q->sq_doorbell = q->sq_tail;

  uint64_t start = hpet_get_millis();

  if (ctrl->use_msix && q->contexts) {
    while (!q->contexts[cid].done) {
      if (ctrl->regs->csts & NVME_CSTS_CFS) {
        kprintf("[NVMe] Fatal: Controller reports fatal status\n");
        return -1;
      }
      if ((hpet_get_millis() - start) >= ctrl->timeout_ms) {
        kprintf("[NVMe] Error: I/O command timeout (CID %u)\n", cid);
        return -1;
      }
      __asm__ volatile("pause");
    }

    __asm__ volatile("" ::: "memory");

    if (cqe_out) {
      *cqe_out = q->contexts[cid].cqe;
    }

    if (q->contexts[cid].status != 0) {
      kprintf("[NVMe] Error: I/O command failed with status 0x%04x\n", q->contexts[cid].status);
      return -1;
    }

    return 0;
  }

  volatile NvmeCqe *cqe = &q->cq[q->cq_head];
  while ((cqe->status & 1) != q->cq_phase) {
    if (ctrl->regs->csts & NVME_CSTS_CFS) {
      kprintf("[NVMe] Fatal: Controller reports fatal status\n");
      return -1;
    }
    if ((hpet_get_millis() - start) >= ctrl->timeout_ms) {
      kprintf("[NVMe] Error: I/O command timeout (CID %u)\n", cid);
      return -1;
    }
    __asm__ volatile("pause");
  }

  if (cqe_out) {
    *cqe_out = *cqe;
  }

  uint16_t status = cqe->status >> 1;
  q->cq_head = (q->cq_head + 1) % q->size;
  if (q->cq_head == 0) {
    q->cq_phase ^= 1;
  }
  *q->cq_doorbell = q->cq_head;

  if (status != 0) {
    kprintf("[NVMe] Error: I/O command failed with status 0x%04x\n", status);
    return -1;
  }

  return 0;
}

static int nvme_setup_io_queue(NvmeController *ctrl, uint16_t qid) {
  uint16_t q_entries = NVME_IO_QUEUE_ENTRIES;
  if (q_entries > ctrl->max_queue_entries) {
    q_entries = (uint16_t)ctrl->max_queue_entries;
  }

  void *sq_frame = pmm_alloc_frame();
  void *cq_frame = pmm_alloc_frame();
  if (!sq_frame || !cq_frame) {
    if (sq_frame) pmm_free_frame(sq_frame);
    if (cq_frame) pmm_free_frame(cq_frame);
    return -1;
  }

  ctrl->io_queue.sq_phys = (uint64_t)sq_frame;
  ctrl->io_queue.cq_phys = (uint64_t)cq_frame;
  ctrl->io_queue.sq = (NvmeSqe *)(ctrl->io_queue.sq_phys + HHDM_BASE);
  ctrl->io_queue.cq = (NvmeCqe *)(ctrl->io_queue.cq_phys + HHDM_BASE);

  memset(ctrl->io_queue.sq, 0, PAGE_SIZE);
  memset(ctrl->io_queue.cq, 0, PAGE_SIZE);

  ctrl->io_queue.contexts = (NvmeCommandContext *)kmalloc(q_entries * sizeof(NvmeCommandContext));
  if (!ctrl->io_queue.contexts) {
    pmm_free_frame(sq_frame);
    pmm_free_frame(cq_frame);
    return -1;
  }
  memset(ctrl->io_queue.contexts, 0, q_entries * sizeof(NvmeCommandContext));

  ctrl->io_queue.size = q_entries;
  ctrl->io_queue.sq_tail = 0;
  ctrl->io_queue.cq_head = 0;
  ctrl->io_queue.cq_phase = 1;

  uint32_t stride = 4 << ctrl->dstrd;
  uint8_t *mmio = (uint8_t *)ctrl->regs;
  ctrl->io_queue.sq_doorbell = (volatile uint32_t *)(mmio + 0x1000 + ((2 * qid + 0) * stride));
  ctrl->io_queue.cq_doorbell = (volatile uint32_t *)(mmio + 0x1000 + ((2 * qid + 1) * stride));

  uint32_t cq_cdw11 = (1 << 0);
  if (ctrl->use_msix) {
    cq_cdw11 |= (1 << 1) | (0 << 16);
  }

  NvmeSqe cmd;

  memset(&cmd, 0, sizeof(cmd));
  cmd.cdw0 = NVME_ADMIN_OP_CREATE_IOCQ;
  cmd.dptr[0] = ctrl->io_queue.cq_phys;
  cmd.cdw10 = ((uint32_t)(q_entries - 1) << 16) | qid;
  cmd.cdw11 = cq_cdw11;
  if (nvme_submit_admin_cmd(ctrl, &cmd, NULL) != 0) {
    kfree(ctrl->io_queue.contexts);
    pmm_free_frame(sq_frame);
    pmm_free_frame(cq_frame);
    return -1;
  }

  memset(&cmd, 0, sizeof(cmd));
  cmd.cdw0 = NVME_ADMIN_OP_CREATE_IOSQ;
  cmd.dptr[0] = ctrl->io_queue.sq_phys;
  cmd.cdw10 = ((uint32_t)(q_entries - 1) << 16) | qid;
  cmd.cdw11 = (1 << 0) | ((uint32_t)qid << 16);
  if (nvme_submit_admin_cmd(ctrl, &cmd, NULL) != 0) {
    kfree(ctrl->io_queue.contexts);
    pmm_free_frame(sq_frame);
    pmm_free_frame(cq_frame);
    return -1;
  }

  return 0;
}

int nvme_read(NvmeController *ctrl, uint32_t nsid, uint64_t lba, uint32_t count, void *buf) {
  if (!ctrl || !buf || count == 0) {
    return -1;
  }

  NvmeNamespace *ns = ctrl->namespaces;
  while (ns && ns->nsid != nsid) {
    ns = ns->next;
  }

  if (!ns || ns->block_size == 0) {
    return -1;
  }

  PageDirectory pml4 = vmm_get_kernel_pml4();

  uint32_t max_chunk_blocks = 0x10000;
  if (ctrl->max_transfer_bytes > 0) {
    uint32_t mdts_blocks = ctrl->max_transfer_bytes / ns->block_size;
    if (mdts_blocks > 0 && mdts_blocks < max_chunk_blocks) {
      max_chunk_blocks = mdts_blocks;
    }
  }

  uint64_t curr_lba = lba;
  uint32_t remaining_blocks = count;
  uintptr_t curr_virt = (uintptr_t)buf;

  while (remaining_blocks > 0) {
    uint32_t chunk_blocks = remaining_blocks;
    if (chunk_blocks > max_chunk_blocks) {
      chunk_blocks = max_chunk_blocks;
    }

    uint32_t chunk_bytes = chunk_blocks * ns->block_size;
    uint64_t prp1 = vmm_virt_to_phys(pml4, curr_virt);
    if (!prp1) {
      return -1;
    }

    uint32_t page_offset = (uint32_t)(curr_virt & (PAGE_SIZE - 1));
    uint32_t first_page_bytes = (uint32_t)(PAGE_SIZE - page_offset);

    NvmeSqe cmd;
    memset(&cmd, 0, sizeof(cmd));
    cmd.cdw0 = NVME_NVM_OP_READ;
    cmd.nsid = nsid;
    cmd.cdw10 = (uint32_t)(curr_lba & 0xFFFFFFFF);
    cmd.cdw11 = (uint32_t)(curr_lba >> 32);
    cmd.cdw12 = chunk_blocks - 1;
    cmd.dptr[0] = prp1;

    void *prp_frames[130];
    size_t num_prp_pages = 0;

    if (chunk_bytes <= first_page_bytes) {
      cmd.dptr[1] = 0;
    } else {
      uintptr_t next_page_virt = curr_virt + first_page_bytes;
      uint32_t bytes_left = chunk_bytes - first_page_bytes;

      if (bytes_left <= PAGE_SIZE) {
        uint64_t prp2 = vmm_virt_to_phys(pml4, next_page_virt);
        if (!prp2) {
          return -1;
        }
        cmd.dptr[1] = prp2;
      } else {
        size_t pages_needed = (bytes_left + PAGE_SIZE - 1) / PAGE_SIZE;
        num_prp_pages = 1;
        if (pages_needed > 512) {
          num_prp_pages = 1 + (pages_needed - 512 + 510) / 511;
        }

        for (size_t i = 0; i < num_prp_pages; i++) {
          prp_frames[i] = pmm_alloc_frame();
          if (!prp_frames[i]) {
            for (size_t j = 0; j < i; j++) {
              pmm_free_frame(prp_frames[j]);
            }
            return -1;
          }
        }

        size_t cur_page = 0;
        size_t cur_entry = 0;
        uint64_t *prp_list = (uint64_t *)((uint64_t)prp_frames[0] + HHDM_BASE);
        memset(prp_list, 0, PAGE_SIZE);

        while (bytes_left > 0) {
          if (cur_entry == 511 && (cur_page + 1) < num_prp_pages) {
            prp_list[511] = (uint64_t)prp_frames[cur_page + 1];
            cur_page++;
            cur_entry = 0;
            prp_list = (uint64_t *)((uint64_t)prp_frames[cur_page] + HHDM_BASE);
            memset(prp_list, 0, PAGE_SIZE);
          }

          uint64_t phys = vmm_virt_to_phys(pml4, next_page_virt);
          if (!phys) {
            for (size_t i = 0; i < num_prp_pages; i++) {
              pmm_free_frame(prp_frames[i]);
            }
            return -1;
          }

          prp_list[cur_entry++] = phys;

          uint32_t step = (bytes_left > PAGE_SIZE) ? (uint32_t)PAGE_SIZE : bytes_left;
          next_page_virt += step;
          bytes_left -= step;
        }

        cmd.dptr[1] = (uint64_t)prp_frames[0];
      }
    }

    if (nvme_submit_io_cmd(ctrl, &cmd, NULL) != 0) {
      for (size_t i = 0; i < num_prp_pages; i++) {
        pmm_free_frame(prp_frames[i]);
      }
      return -1;
    }

    for (size_t i = 0; i < num_prp_pages; i++) {
      pmm_free_frame(prp_frames[i]);
    }

    curr_virt += chunk_bytes;
    curr_lba += chunk_blocks;
    remaining_blocks -= chunk_blocks;
  }

  return 0;
}

static void nvme_init_device(const PciDevice *dev) {
  if (dev->bars[0].is_io || dev->bars[0].phys_addr == 0) {
    kprintf("[NVMe] Error: Invalid BAR0 on %02x:%02x.%u\n", dev->bus, dev->device, dev->function);
    return;
  }

  pcie_enable_bus_master(dev);

  uint64_t bar_phys = dev->bars[0].phys_addr;
  uint64_t bar_size = dev->bars[0].size;
  if (bar_size < 2 * PAGE_SIZE) {
    bar_size = 2 * PAGE_SIZE;
  }

  PageDirectory pml4 = vmm_get_kernel_pml4();
  uint64_t bar_virt = bar_phys + HHDM_BASE;

  if (vmm_map_range(pml4, bar_virt, bar_phys, bar_size, VMM_FLAG_PRESENT | VMM_FLAG_WRITABLE | VMM_FLAG_NO_CACHE) != 0) {
    kprintf("[NVMe] Error: Failed to map MMIO BAR0 on %02x:%02x.%u\n", dev->bus, dev->device, dev->function);
    return;
  }

  NvmeController *ctrl = (NvmeController *)kzalloc(sizeof(NvmeController));
  if (!ctrl) {
    kprintf("[NVMe] Error: Failed to allocate controller structure\n");
    return;
  }

  ctrl->pci_dev = dev;
  ctrl->regs = (NvmeRegisters *)bar_virt;
  ctrl->bar_phys = bar_phys;
  ctrl->bar_size = bar_size;
  ctrl->cap = ctrl->regs->cap;
  ctrl->vs = ctrl->regs->vs;
  ctrl->dstrd = (uint32_t)((ctrl->cap >> 32) & 0xF);
  ctrl->max_queue_entries = (uint32_t)(ctrl->cap & 0xFFFF) + 1;

  uint8_t cap_to = (uint8_t)((ctrl->cap >> 24) & 0xFF);
  ctrl->timeout_ms = (cap_to == 0) ? 1000 : (uint32_t)cap_to * 500;

  ctrl->use_msix = 0;
  if (pcie_find_capability(dev, PCI_CAP_ID_MSIX)) {
    if (pcie_enable_msix(dev) == 0) {
      if (pcie_enable_msix_vector(dev, 0, NVME_IRQ_VECTOR, lapic_get_id()) == 0) {
        ctrl->use_msix = 1;
      }
    }
  }

  if (nvme_reset_controller(ctrl) != 0) {
    kprintf("[NVMe] Error: Controller reset failed\n");
    kfree(ctrl);
    return;
  }

  if (nvme_setup_admin_queue(ctrl) != 0) {
    kprintf("[NVMe] Error: Admin queue allocation failed\n");
    kfree(ctrl);
    return;
  }

  if (nvme_enable_controller(ctrl) != 0) {
    kprintf("[NVMe] Error: Failed to enable controller\n");
    pmm_free_frame((void *)ctrl->admin_queue.sq_phys);
    pmm_free_frame((void *)ctrl->admin_queue.cq_phys);
    kfree(ctrl);
    return;
  }

  ctrl->namespaces = NULL;

  if (nvme_identify(ctrl) != 0) {
    kprintf("[NVMe] Error: Identify failed\n");
    pmm_free_frame((void *)ctrl->admin_queue.sq_phys);
    pmm_free_frame((void *)ctrl->admin_queue.cq_phys);
    kfree(ctrl);
    return;
  }

  if (nvme_setup_io_queue(ctrl, 1) != 0) {
    kprintf("[NVMe] Error: Failed to create I/O queue pair\n");
    NvmeNamespace *ns = ctrl->namespaces;
    while (ns) {
      NvmeNamespace *next = ns->next;
      kfree(ns);
      ns = next;
    }
    pmm_free_frame((void *)ctrl->admin_queue.sq_phys);
    pmm_free_frame((void *)ctrl->admin_queue.cq_phys);
    kfree(ctrl);
    return;
  }

  ctrl->next = g_controllers;
  g_controllers = ctrl;

  uint32_t major = (ctrl->vs >> 16) & 0xFFFF;
  uint32_t minor = (ctrl->vs >> 8) & 0xFF;
  uint32_t tertiary = ctrl->vs & 0xFF;

  kprintf("[NVMe] Controller @ %02x:%02x.%u / Spec %u.%u.%u / Admin Q: %u entries\n",
          dev->bus, dev->device, dev->function,
          major, minor, tertiary,
          ctrl->admin_queue.size);
}

void nvme_init(void) {
  idt_register_handler(NVME_IRQ_VECTOR, nvme_irq_handler);
  g_controllers = NULL;
  size_t count = pcie_get_device_count();

  for (size_t i = 0; i < count; i++) {
    const PciDevice *dev = pcie_get_device(i);
    if (dev && dev->class_code == PCI_CLASS_STORAGE &&
      dev->subclass == PCI_SUBCLASS_NVM &&
      dev->prog_if == PCI_PROGIF_NVME) {
      nvme_init_device(dev);
      }
  }
}


NvmeController *nvme_get_controllers(void) {
  return g_controllers;
}
