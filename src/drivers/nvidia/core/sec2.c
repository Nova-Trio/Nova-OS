#include "nv_sec2.h"
#include <nv_reg.h>
#include <string.h>

#define ALIGN_UP(x, a) (((x) + ((a) - 1ULL)) & ~((a) - 1ULL))

typedef struct {
  uint32_t sig_prod_offset;
  uint32_t sig_prod_size;
  uint32_t patch_loc;
  uint32_t patch_sig;
  uint32_t meta_data_offset;
  uint32_t meta_data_size;
  uint32_t num_sig;
  uint32_t header_offset;
  uint32_t header_size;
} __attribute__((packed)) NvHsHeaderV2;

typedef struct {
  uint32_t os_code_offset;
  uint32_t os_code_size;
  uint32_t os_data_offset;
  uint32_t os_data_size;
  uint32_t num_apps;
  struct {
    uint32_t offset;
    uint32_t size;
    uint32_t data_offset;
    uint32_t data_size;
  } app[1];
} __attribute__((packed)) NvHsLoadHeaderV2;

int nv_sec2_init(const NvDevice *dev, NvSec2Context *sec2) {
  if (!dev || !sec2 || !dev->bar0.virt_addr) {
    kprintf("[NV/SEC2] Error: Invalid device or SEC2 context\n");
    return -1;
  }

  memset(sec2, 0, sizeof(NvSec2Context));

  nv_falcon_init(&sec2->falcon, NV_SEC2_BASE, NV_SEC2_FBIF_BASE, 0, false, "SEC2");

  int res = nv_falcon_reset(dev, &sec2->falcon);
  if (res != 0) {
    kprintf("[NV/SEC2] Error: Failed to reset SEC2 Falcon engine (%d)\n", res);
    return res;
  }

  uint32_t hwcfg = nv_rd32(dev, sec2->falcon.base_addr + NV_FALCON_HWCFG);
  sec2->imem_size = (hwcfg & NV_FALCON_HWCFG_IMEM_SIZE_MASK) * NV_FALCON_IMEM_BLK_SIZE;

  kprintf("[NV/SEC2] SEC2 Falcon initialized (IMEM: %u KB)\n", sec2->imem_size / 1024);
  return 0;
}

int nv_sec2_stage_booter(NvSec2Context *sec2) {
  if (!sec2) {
    return -1;
  }

  void *raw_booter = NULL;
  size_t booter_file_size = 0;

  if (fs_read_file("/nova/fw/booter_load.bin", &raw_booter, &booter_file_size) != 0 || !raw_booter) {
    kprintf("[NV/SEC2] Error: Failed to read /nova/fw/booter_load.bin from filesystem\n");
    return -1;
  }

  if (booter_file_size < sizeof(NvGspBinHdr)) {
    kprintf("[NV/SEC2] Error: Invalid booter_load.bin file size\n");
    kfree(raw_booter);
    return -1;
  }

  const NvGspBinHdr *hdr = (const NvGspBinHdr *)raw_booter;
  if (hdr->bin_magic != 0x000010deU && hdr->bin_magic != 0x3b1d14f0U) {
    kprintf("[NV/SEC2] Error: Invalid booter container magic (0x%08x)\n", hdr->bin_magic);
    kfree(raw_booter);
    return -1;
  }

  if (nv_dma_alloc(&sec2->ucode_fw, booter_file_size) != 0) {
    kprintf("[NV/SEC2] Error: Failed to allocate DMA buffer for SEC2 booter\n");
    kfree(raw_booter);
    return -1;
  }

  memcpy((void *)sec2->ucode_fw.virt_addr, raw_booter, booter_file_size);
  sec2->ucode_size = (uint32_t)booter_file_size;

  kfree(raw_booter);

  kprintf("[NV/SEC2] Staged booter_load.bin (%u bytes) at PA 0x%016llx\n", sec2->ucode_size, sec2->ucode_fw.phys_addr);

  return 0;
}

int nv_sec2_execute_booter_load(const NvDevice *dev, const NvSec2Context *sec2, const NvGspContext *gsp) {
  if (!dev || !sec2 || !gsp || !dev->bar0.virt_addr || !sec2->ucode_fw.virt_addr || !gsp->wpr_meta.phys_addr) {
    kprintf("[NV/SEC2] Error: Invalid execution parameters for booter_load\n");
    return -1;
  }

  const uint8_t *bl_data = (const uint8_t *)sec2->ucode_fw.virt_addr;
  const NvGspBinHdr *hdr = (const NvGspBinHdr *)bl_data;

  const NvHsHeaderV2 *hs_hdr = (const NvHsHeaderV2 *)(bl_data + hdr->header_offset);

  uint32_t load_hdr_offset = hs_hdr->header_offset;
  if (load_hdr_offset < hdr->header_offset) {
    load_hdr_offset += hdr->header_offset;
  }
  const NvHsLoadHeaderV2 *load_hdr = (const NvHsLoadHeaderV2 *)(bl_data + load_hdr_offset);
  uint8_t *payload = (uint8_t *)(bl_data + hdr->data_offset);

  uint32_t patch_loc = *(const uint32_t *)(bl_data + hs_hdr->patch_loc);
  uint32_t patch_sig = *(const uint32_t *)(bl_data + hs_hdr->patch_sig);

  kprintf("[NV/SEC2] HS v2 Booter: OS Code=0x%x (sz %u), OS Data=0x%x (sz %u), Apps=%u\n", load_hdr->os_code_offset, load_hdr->os_code_size, load_hdr->os_data_offset, load_hdr->os_data_size,
          load_hdr->num_apps);
  kprintf("[NV/SEC2] Signature Patch: SigProd=0x%x (sz %u), patch_loc=0x%x, patch_sig=0x%x\n", hs_hdr->sig_prod_offset, hs_hdr->sig_prod_size, patch_loc, patch_sig);

  if (hs_hdr->sig_prod_size > 0 && (patch_loc + hs_hdr->sig_prod_size) <= hdr->data_size) {
    const uint8_t *sig_src = bl_data + hs_hdr->sig_prod_offset + patch_sig;
    memcpy(payload + patch_loc, sig_src, hs_hdr->sig_prod_size);
  }

  if (nv_falcon_reset(dev, &sec2->falcon) != 0) {
    kprintf("[NV/SEC2] Error: SEC2 reset failed prior to execution\n");
    return -1;
  }

  if (nv_falcon_setup_fbif_aperture(dev, &sec2->falcon,
    FALCON_DMAIDX_PHYS_SYS_COH,
    NV_FALCON_FBIF_TRANSCFG_TARGET_COHERENT_SYSMEM,
    NV_FALCON_FBIF_TRANSCFG_MEM_TYPE_PHYSICAL) != 0) {
    kprintf("[NV/SEC2] Error: Failed to setup SEC2 FBIF DMA Aperture 4\n");
    return -1;
  }

  if (load_hdr->os_code_size > 0) {
    if (nv_falcon_imem_write(dev, &sec2->falcon, 0,
      payload + load_hdr->os_code_offset,
      load_hdr->os_code_size,
      false, load_hdr->os_code_offset) != 0) {
      kprintf("[NV/SEC2] Error: Failed to write OS code to SEC2 IMEM\n");
    return -1;
      }
  }

  uint32_t cur_imem_dst = ALIGN_UP(load_hdr->os_code_size, 256ULL);
  for (uint32_t i = 0; i < load_hdr->num_apps; i++) {
    if (load_hdr->app[i].size > 0) {
      if (nv_falcon_imem_write(dev, &sec2->falcon, cur_imem_dst,
        payload + load_hdr->app[i].offset,
        load_hdr->app[i].size,
        true, load_hdr->app[i].offset) != 0) {
        kprintf("[NV/SEC2] Error: Failed to write secure app[%u] to SEC2 IMEM\n", i);
      return -1;
        }
        cur_imem_dst = ALIGN_UP(cur_imem_dst + load_hdr->app[i].size, 256ULL);
    }
  }

  if (load_hdr->os_data_size > 0) {
    if (nv_falcon_dmem_write(dev, &sec2->falcon, 0,
      payload + load_hdr->os_data_offset,
      load_hdr->os_data_size) != 0) {
      kprintf("[NV/SEC2] Error: Failed to write patched DMEM data to SEC2\n");
    return -1;
      }
  }

  for (uint32_t i = 0; i < load_hdr->num_apps; i++) {
    if (load_hdr->app[i].data_size > 0) {
      if (nv_falcon_dmem_write(dev, &sec2->falcon, load_hdr->app[i].data_offset,
        payload + load_hdr->app[i].data_offset,
        load_hdr->app[i].data_size) != 0) {
        kprintf("[NV/SEC2] Error: Failed to write app[%u] data to SEC2 DMEM\n", i);
      return -1;
        }
    }
  }

  nv_falcon_set_bootvec(dev, &sec2->falcon, 0x00000000U);

  uint32_t mbox0 = (uint32_t)(gsp->wpr_meta.phys_addr & 0xFFFFFFFFU);
  uint32_t mbox1 = (uint32_t)(gsp->wpr_meta.phys_addr >> 32);

  nv_falcon_mailbox_write(dev, &sec2->falcon, 0, mbox0);
  nv_falcon_mailbox_write(dev, &sec2->falcon, 1, mbox1);

  kprintf("[NV/SEC2] Starting SEC2 CPU (MAILBOX0=0x%08x, MAILBOX1=0x%08x)...\n", mbox0, mbox1);

  nv_falcon_start_cpu(dev, &sec2->falcon);

  if (nv_falcon_wait_halt(dev, &sec2->falcon, 2000000U) != 0) {
    uint32_t err_mbox0 = nv_falcon_mailbox_read(dev, &sec2->falcon, 0);
    uint32_t err_mbox1 = nv_falcon_mailbox_read(dev, &sec2->falcon, 1);
    uint32_t cpuctl = nv_rd32(dev, sec2->falcon.base_addr + NV_FALCON_CPUCTL);
    kprintf("[NV/SEC2] Error: Booter execution timed out (CPUCTL=0x%08x, MAILBOX0=0x%08x, MAILBOX1=0x%08x)\n",
            cpuctl, err_mbox0, err_mbox1);
    return -1;
  }

  uint32_t res_mbox0 = nv_falcon_mailbox_read(dev, &sec2->falcon, 0);
  uint32_t res_mbox1 = nv_falcon_mailbox_read(dev, &sec2->falcon, 1);

  if (res_mbox0 != NV_SEC2_BOOTER_SUCCESS) {
    kprintf("[NV/SEC2] Error: Booter failed with error code 0x%08x (MAILBOX1=0x%08x)\n",
            res_mbox0, res_mbox1);
    return -1;
  }

  if (!nv_wpr_is_wpr2_up(dev)) {
    kprintf("[NV/SEC2] Error: WPR2 aperture is not enabled in PFB MMU after booter execution\n");
    return -1;
  }

  kprintf("[NV/SEC2] SEC2 Booter Load completed successfully (MAILBOX0=0x%08x, WPR2=ACTIVE).\n", res_mbox0);
  return 0;
}

void nv_sec2_cleanup(NvSec2Context *sec2) {
  if (!sec2) {
    return;
  }

  if (sec2->ucode_fw.phys_addr) {
    nv_dma_free(&sec2->ucode_fw);
  }

  memset(sec2, 0, sizeof(NvSec2Context));
}
