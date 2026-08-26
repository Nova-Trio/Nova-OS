#include <nv_wpr.h>
#include <novamod.h>


bool nv_wpr_is_wpr2_up(const NvDevice *dev) {
  if (!dev || !dev->bar0.virt_addr) {
    return false;
  }

  uint32_t val = nv_rd32(dev, NV_PFB_PRI_MMU_WPR2_ADDR_HI);
  return (val & NV_PFB_PRI_MMU_WPR2_ADDR_HI_VAL) != 0;
}

uint64_t nv_fb_get_real_size(const NvDevice *dev) {
  if (!dev || !dev->bar0.virt_addr) {
    return 0;
  }

  uint32_t fbpas = nv_rd32(dev, 0x022458);
  uint32_t fbp_mask = nv_rd32(dev, 0x021d38);

  uint64_t total_bytes = 0;
  uint32_t fbpa_idx = 0;

  for (uint32_t fbp = 0; fbp < 4; fbp++) {
    if (!(fbp_mask & (1U << fbp))) {
      for (uint32_t i = 0; i < fbpas; i++) {
        uint32_t size_mb = nv_rd32(dev, 0x90020c + (fbpa_idx * 0x4000));
        total_bytes += (uint64_t)size_mb * 1024ULL * 1024ULL;
        fbpa_idx++;
      }
    }
  }

  if (total_bytes >= 0x40000000ULL) {
    return total_bytes;
  }

  return 0x0ULL;
}

int nv_wpr_populate_meta(const NvDevice *dev, NvGspContext *gsp, uint64_t fb_size) {
  if (!dev || !gsp || !gsp->wpr_meta.virt_addr || fb_size == 0) {
    kprintf("[NV/WPR] Error: Invalid device, GSP context, or FB size\n");
    return -1;
  }

  if (gsp->radix3.fw_size == 0 || gsp->boot_fw.size == 0 || gsp->sig.size == 0) {
    kprintf("[NV/WPR] Error: staging buffers not initialized\n");
    return -1;
  }


  uint64_t vga_workspace_size = GSP_VBIOS_WORKSPACE_SIZE;
  uint64_t vga_workspace_offset = fb_size - vga_workspace_size;

  uint64_t gsp_fw_wpr_end = GSP_ALIGN_DOWN(vga_workspace_offset, GSP_WPR_ALIGNMENT);

  uint64_t frts_size = GSP_FRTS_SIZE_TU102;
  uint64_t frts_offset = gsp_fw_wpr_end - frts_size;

  uint64_t size_of_bootloader = gsp->boot_fw.size;
  uint64_t boot_bin_offset = GSP_ALIGN_DOWN(frts_offset - size_of_bootloader, 0x1000ULL);

  uint64_t size_of_radix3_elf = gsp->radix3.fw_size;
  uint64_t gsp_fw_offset = GSP_ALIGN_DOWN(boot_bin_offset - size_of_radix3_elf, 0x10000ULL);

  //kprintf("[NV/WPR] boot_bin_offset = 0x%016llx\n", boot_bin_offset);
  //kprintf("[NV/WPR] size_of_radix3_elf = %llu bytes (0x%llx)\n", size_of_radix3_elf, size_of_radix3_elf);
  //kprintf("[NV/WPR] subtraction raw  = 0x%016llx\n", boot_bin_offset - size_of_radix3_elf);
  //kprintf("[NV/WPR] gsp_fw_offset   = 0x%016llx\n", gsp_fw_offset);

  uint64_t wpr_heap_size = GSP_WPR_HEAP_SIZE_TU10X_DEFAULT;
  uint64_t gsp_fw_heap_offset = GSP_ALIGN_DOWN(gsp_fw_offset - wpr_heap_size, 0x100000ULL);
  uint64_t gsp_fw_heap_size = gsp_fw_offset - gsp_fw_heap_offset;

  uint64_t gsp_fw_wpr_start = gsp_fw_heap_offset - GSP_WPR_META_SIZE;

  uint64_t non_wpr_heap_size = GSP_NON_WPR_HEAP_SIZE_DEFAULT;
  uint64_t non_wpr_heap_offset = gsp_fw_wpr_start - non_wpr_heap_size;
  uint64_t gsp_fw_rsvd_start = non_wpr_heap_offset;

  if (non_wpr_heap_offset >= fb_size || gsp_fw_wpr_start >= gsp_fw_wpr_end) {
    kprintf("[NV/WPR] Error: Calculated FB carveout exceeds physical FB size (0x%llx >= 0x%llx)\n", non_wpr_heap_offset, fb_size);
    return -1;
  }

  GspFwWprMeta *meta = (GspFwWprMeta *)gsp->wpr_meta.virt_addr;
  memset(meta, 0, sizeof(GspFwWprMeta));

  meta->magic = GSP_FW_WPR_META_MAGIC;
  meta->revision = GSP_FW_WPR_META_REVISION;

  meta->sysmemAddrOfRadix3Elf = gsp->radix3.lvl0.phys_addr;
  meta->sizeOfRadix3Elf = size_of_radix3_elf;

  meta->sysmemAddrOfBootloader = gsp->boot_fw.phys_addr;
  meta->sizeOfBootloader = size_of_bootloader;

  meta->bootloaderCodeOffset = gsp->boot_desc.monitorCodeOffset;
  meta->bootloaderDataOffset = gsp->boot_desc.monitorDataOffset;
  meta->bootloaderManifestOffset = gsp->boot_desc.manifestOffset;

  meta->sysmemAddrOfSignature = gsp->sig.phys_addr;
  meta->sizeOfSignature = gsp->sig.size;

  meta->gspFwRsvdStart = gsp_fw_rsvd_start;
  meta->nonWprHeapOffset = non_wpr_heap_offset;
  meta->nonWprHeapSize = non_wpr_heap_size;

  meta->gspFwWprStart = gsp_fw_wpr_start;
  meta->gspFwHeapOffset = gsp_fw_heap_offset;
  meta->gspFwHeapSize = gsp_fw_heap_size;

  meta->gspFwOffset = gsp_fw_offset;
  meta->bootBinOffset = boot_bin_offset;

  meta->frtsOffset = frts_offset;
  meta->frtsSize = frts_size;

  meta->gspFwWprEnd = gsp_fw_wpr_end;
  meta->fbSize = fb_size;

  meta->vgaWorkspaceOffset = vga_workspace_offset;
  meta->vgaWorkspaceSize = vga_workspace_size;

  meta->bootCount = 0;
  meta->gspFwHeapVfPartitionCount = 0;
  meta->flags = 0;
  meta->pmuReservedSize = 0;
  meta->verified = GSP_FW_WPR_META_UNVERIFIED;

  nv_dma_wmb();

  kprintf("[NV/WPR] Metadata staged successfully at PA 0x%016llx (WPR2: 0x%08llx - 0x%08llx)\n", gsp->wpr_meta.phys_addr, meta->gspFwWprStart, meta->gspFwWprEnd);

  return 0;
}

void nv_wpr_dump_meta(const GspFwWprMeta *meta) {
  if (!meta) {
    return;
  }

  kprintf("[NV/WPR] WPR layout \n");
  kprintf("[NV/WPR] Magic                   : 0x%016llx (rev %llu)\n", meta->magic, meta->revision);
  kprintf("[NV/WPR] Total FB Size           : %llu MB (0x%016llx)\n", meta->fbSize / (1024 * 1024), meta->fbSize);
  kprintf("[NV/WPR] VGA Workspace           : 0x%016llx - 0x%016llx (%llu KB)\n", meta->vgaWorkspaceOffset, meta->vgaWorkspaceOffset + meta->vgaWorkspaceSize - 1, meta->vgaWorkspaceSize / 1024);
  kprintf("[NV/WPR] WPR2 Window             : 0x%016llx - 0x%016llx (%llu MB)\n", meta->gspFwWprStart, meta->gspFwWprEnd - 1, (meta->gspFwWprEnd - meta->gspFwWprStart) / (1024 * 1024));
  kprintf("[NV/WPR]   FRTS Region           : 0x%016llx - 0x%016llx (%llu KB)\n", meta->frtsOffset, meta->frtsOffset + meta->frtsSize - 1, meta->frtsSize / 1024);
  kprintf("[NV/WPR]   Bootloader Binary     : 0x%016llx - 0x%016llx (%llu B)\n", meta->bootBinOffset, meta->bootBinOffset + meta->sizeOfBootloader - 1, meta->sizeOfBootloader);
  kprintf("[NV/WPR]   GSP FW ELF            : 0x%016llx - 0x%016llx (%llu KB)\n", meta->gspFwOffset, meta->gspFwOffset + meta->sizeOfRadix3Elf - 1, meta->sizeOfRadix3Elf / 1024);
  kprintf("[NV/WPR]   GSP-RM WPR Heap       : 0x%016llx - 0x%016llx (%llu MB)\n", meta->gspFwHeapOffset, meta->gspFwHeapOffset + meta->gspFwHeapSize - 1, meta->gspFwHeapSize / (1024 * 1024));
  kprintf("[NV/WPR]   WPR Meta Header       : 0x%016llx - 0x%016llx (1 MB)\n", meta->gspFwWprStart, meta->gspFwHeapOffset - 1);
  kprintf("[NV/WPR] Non-WPR Heap            : 0x%016llx - 0x%016llx (%llu MB)\n", meta->nonWprHeapOffset, meta->nonWprHeapOffset + meta->nonWprHeapSize - 1, meta->nonWprHeapSize / (1024 * 1024));
  kprintf("[NV/WPR] SYSMEM Radix-3 Root PA  : 0x%016llx\n", meta->sysmemAddrOfRadix3Elf);
  kprintf("[NV/WPR] SYSMEM Bootloader PA    : 0x%016llx\n", meta->sysmemAddrOfBootloader);
  kprintf("[NV/WPR] SYSMEM Signature PA     : 0x%016llx\n", meta->sysmemAddrOfSignature);
  kprintf("[NV/WPR] Verified Token          : 0x%016llx (%s)\n", meta->verified, (meta->verified == GSP_FW_WPR_META_VERIFIED) ? "VERIFIED" : "UNVERIFIED");
  kprintf("[NV/WPR] **END** \n");
}
