#include <nv_bios.h>
#include <novamod.h>
#include <stdbool.h>
#include <nv_falcon.h>
#include <nv_wpr.h>

static inline uint32_t prom_rd32_raw(const NvDevice *dev, uint32_t offset) {
  return nv_rd32(dev, NV_PROM_BASE + offset);
}

static uint32_t prom_read(const NvDevice *dev, uint32_t offset, uint8_t size) {
  uint32_t aligned_offset = offset & ~0x3U;
  uint32_t byte_shift = (offset & 0x3U) * 8;

  uint32_t lo = prom_rd32_raw(dev, aligned_offset);
  if ((offset & 0x3U) + size <= 4) {
    if (size == 4) return lo;
    return (lo >> byte_shift) & ((1U << (size * 8)) - 1);
  }

  uint32_t hi = prom_rd32_raw(dev, aligned_offset + 4);
  uint64_t combined = ((uint64_t)hi << 32) | lo;
  return (uint32_t)((combined >> byte_shift) & ((1U << (size * 8)) - 1));
}

static inline uint8_t  prom_rd08(const NvDevice *dev, uint32_t offset) { return (uint8_t)prom_read(dev, offset, 1); }
static inline uint16_t prom_rd16(const NvDevice *dev, uint32_t offset) { return (uint16_t)prom_read(dev, offset, 2); }
static inline uint32_t prom_rd32(const NvDevice *dev, uint32_t offset) { return prom_read(dev, offset, 4); }

static inline uint8_t bios_rd08(const NvBios *bios, uint32_t offset) {
  return (offset < bios->size) ? bios->data[offset] : 0;
}

static inline uint16_t bios_rd16(const NvBios *bios, uint32_t offset) {
  return (offset + 1 < bios->size) ? *(const uint16_t *)&bios->data[offset] : 0;
}

static inline uint32_t bios_rd32(const NvBios *bios, uint32_t offset) {
  return (offset + 3 < bios->size) ? *(const uint32_t *)&bios->data[offset] : 0;
}

static int nv_bios_find_pci_header(const NvDevice *dev, uint32_t *out_pci_offset) {
  uint16_t sig = prom_rd16(dev, 0x0);
  if (IS_VALID_PCI_ROM_SIG(sig)) {
    *out_pci_offset = 0;
    return 0;
  }

  uint32_t fixed0 = prom_rd32(dev, 0x00);
  uint32_t fixed1 = prom_rd32(dev, 0x04);
  uint32_t fixed2 = prom_rd32(dev, 0x08);

  if (fixed0 == NV_PBUS_IFR_FMT_FIXED0_SIG) {
    uint8_t ifr_version = (fixed1 >> 16) & 0xFF;
    if (ifr_version == 1 || ifr_version == 2) {
      uint32_t ext_offset = fixed1 & 0xFFFF;
      *out_pci_offset = prom_rd32(dev, ext_offset + 4);
      return 0;
    } else if (ifr_version == 3) {
      uint32_t total_data_size = fixed2 & 0xFFFF;
      uint32_t flash_status_offset = prom_rd32(dev, total_data_size);
      uint32_t rom_dir_offset = flash_status_offset + 4096;
      uint32_t rom_dir_sig = prom_rd32(dev, rom_dir_offset);

      if (rom_dir_sig == NV_ROM_DIRECTORY_IDENTIFIER) {
        *out_pci_offset = prom_rd32(dev, rom_dir_offset + 8);
        return 0;
      }
    }
  }

  return -1;
}

static int nv_bios_find_bit(NvBios *bios) {
  if (!bios || !bios->data || bios->size < 12) {
    return -1;
  }

  const uint8_t bit_sig[5] = { 0xFF, 0xB8, 'B', 'I', 'T' };

  for (size_t i = 0; i <= bios->size - 5; i++) {
    if (bios->data[i] == bit_sig[0] &&
      bios->data[i + 1] == bit_sig[1] &&
      bios->data[i + 2] == bit_sig[2] &&
      bios->data[i + 3] == bit_sig[3] &&
      bios->data[i + 4] == bit_sig[4]) {
      bios->bit_offset = (uint32_t)i;
    return 0;
      }
  }

  return -1;
}

int nv_bios_init(const NvDevice *dev, NvBios *bios) {
  if (!dev || !bios) {
    return -1;
  }

  bios->data = NULL;
  bios->size = 0;
  bios->bit_offset = 0;

  uint32_t pci_offset = 0;
  if (nv_bios_find_pci_header(dev, &pci_offset) != 0) {
    kprintf("[VBIOS] Error: Could not locate valid PCI ROM or IFR header\n");
    return -1;
  }

  uint32_t curr_block = pci_offset;
  uint32_t total_size = 0;
  bool last = false;

  while (!last) {
    uint16_t sig = prom_rd16(dev, curr_block);
    if (!IS_VALID_PCI_ROM_SIG(sig)) {
      kprintf("[VBIOS] Error: Invalid ROM signature 0x%04x at offset 0x%x\n", sig, curr_block);
      return -1;
    }

    uint16_t pcir_ptr = prom_rd16(dev, curr_block + 0x18);
    uint32_t pcir_offset = curr_block + pcir_ptr;
    uint32_t pcir_sig = prom_rd32(dev, pcir_offset);

    if (!IS_VALID_PCI_DATA_SIG(pcir_sig)) {
      kprintf("[VBIOS] Error: Invalid PCIR signature 0x%08x at offset 0x%x\n", pcir_sig, pcir_offset);
      return -1;
    }

    uint16_t pcir_len = prom_rd16(dev, pcir_offset + 0x0A);
    uint16_t image_len = prom_rd16(dev, pcir_offset + 0x10);
    uint8_t code_type = prom_rd08(dev, pcir_offset + 0x14);
    last = (prom_rd08(dev, pcir_offset + 0x15) & PCI_LAST_IMAGE_FLAG) != 0;

    uint32_t subimage_size = image_len * PCI_ROM_IMAGE_BLOCK_SIZE;

    if (code_type != 0x70) {
      uint32_t npde_offset = (pcir_offset + pcir_len + 0xF) & ~0xFU;
      uint32_t npde_sig = prom_rd32(dev, npde_offset);

      if (npde_sig == NV_PCI_DATA_EXT_SIG) {
        uint16_t sub_len = prom_rd16(dev, npde_offset + 0x08);
        subimage_size = sub_len * PCI_ROM_IMAGE_BLOCK_SIZE;
        last = (prom_rd08(dev, npde_offset + 0x0A) & PCI_LAST_IMAGE_FLAG) != 0;
      }
    } else {
      last = true;
    }

    curr_block += subimage_size;
    total_size = curr_block - pci_offset;
  }

  if (total_size == 0 || total_size > (1024 * 1024)) {
    kprintf("[VBIOS] Error: Corrupted VBIOS image size %u bytes\n", total_size);
    return -1;
  }

  uint8_t *buffer = (uint8_t *)kmalloc(total_size);
  if (!buffer) {
    kprintf("[VBIOS] Error: Failed to allocate %u bytes for VBIOS buffer\n", total_size);
    return -1;
  }

  uint32_t *dst = (uint32_t *)buffer;
  for (uint32_t i = 0; i < (total_size / 4); i++) {
    dst[i] = prom_rd32_raw(dev, pci_offset + (i * 4));
  }

  bios->data = buffer;
  bios->size = total_size;

  if (nv_bios_find_bit(bios) != 0) {
    kprintf("[VBIOS] Error: BIT table signature '\\xff\\xb8BIT' not found in VBIOS image\n");
    kfree(buffer);
    bios->data = NULL;
    bios->size = 0;
    return -1;
  }

  return 0;
}

void nv_bios_free(NvBios *bios) {
  if (bios && bios->data) {
    kfree(bios->data);
    bios->data = NULL;
    bios->size = 0;
    bios->bit_offset = 0;
  }
}

int nv_bios_get_bit_entry(const NvBios *bios, uint8_t id, NvBitEntry *entry) {
  if (!bios || !bios->data || !bios->bit_offset || !entry) {
    return -1;
  }

  uint8_t entries = bios_rd08(bios, bios->bit_offset + 10);
  uint8_t stride  = bios_rd08(bios, bios->bit_offset + 9);
  uint32_t ptr    = bios->bit_offset + 12;

  if (stride == 0) {
    stride = 6;
  }

  while (entries--) {
    if (bios_rd08(bios, ptr + 0) == id) {
      entry->id = bios_rd08(bios, ptr + 0);
      entry->version = bios_rd08(bios, ptr + 1);
      entry->length = bios_rd16(bios, ptr + 2);
      entry->offset = bios_rd16(bios, ptr + 4);
      return 0;
    }
    ptr += stride;
  }

  return -1;
}

int nv_bios_extract_fwsec(const NvBios *bios, NvFwsecImage *fwsec) {
  if (!bios || !bios->data || !fwsec) {
    return -1;
  }

  memset(fwsec, 0, sizeof(NvFwsecImage));

  NvBitEntry bit_p;
  if (nv_bios_get_bit_entry(bios, 'p', &bit_p) != 0) {
    kprintf("[VBIOS] Error: BIT token 'p' (PMU) not found\n");
    return -1;
  }

  if (bit_p.version != 2 || bit_p.length < 4) {
    kprintf("[VBIOS] Error: Unsupported PMU BIT version %u (len %u)\n", bit_p.version, bit_p.length);
    return -1;
  }

  uint32_t pmu_table = bios_rd32(bios, bit_p.offset);
  if (!pmu_table || pmu_table >= bios->size) {
    kprintf("[VBIOS] Error: Invalid PMU table pointer 0x%x\n", pmu_table);
    return -1;
  }

  uint8_t hdr_size = bios_rd08(bios, pmu_table + 1);
  uint8_t entry_len = bios_rd08(bios, pmu_table + 2);
  uint8_t entry_count = bios_rd08(bios, pmu_table + 3);

  uint32_t fwsec_desc_offset = 0;
  for (uint8_t i = 0; i < entry_count; i++) {
    uint32_t entry_ptr = pmu_table + hdr_size + (i * entry_len);
    uint8_t type = bios_rd08(bios, entry_ptr + 0);
    if (type == NV_PMU_UCODE_TYPE_FWSEC) {
      fwsec_desc_offset = bios_rd32(bios, entry_ptr + 2);
      break;
    }
  }

  if (!fwsec_desc_offset || fwsec_desc_offset >= bios->size) {
    kprintf("[VBIOS] Error: FWSEC descriptor (type 0x85) not found\n");
    return -1;
  }

  uint32_t hdr = bios_rd32(bios, fwsec_desc_offset);
  if (!(hdr & 0x00000001U)) {
    kprintf("[VBIOS] Error: Invalid FWSEC descriptor header 0x%08x\n", hdr);
    return -1;
  }

  uint16_t desc_size = (uint16_t)((hdr >> 16) & 0xFFFF);
  uint8_t desc_ver = (uint8_t)((hdr >> 8) & 0xFF);

  fwsec->version = desc_ver;

  if (desc_ver == 2) {
    memcpy(&fwsec->raw.v2, bios->data + fwsec_desc_offset, sizeof(NvFwsecDescV2));

    fwsec->imem_phys_base = fwsec->raw.v2.imem_phys_base;
    fwsec->imem_load_size = fwsec->raw.v2.imem_load_size;
    fwsec->imem_sec_base = fwsec->raw.v2.imem_sec_base;
    fwsec->imem_sec_size = fwsec->raw.v2.imem_sec_size;

    fwsec->dmem_offset = fwsec->raw.v2.dmem_offset;
    fwsec->dmem_phys_base = fwsec->raw.v2.dmem_phys_base;
    fwsec->dmem_load_size = fwsec->raw.v2.dmem_load_size;

    fwsec->interface_offset = fwsec->raw.v2.interface_offset;
    fwsec->pkc_data_offset = 0;
    fwsec->signatures = NULL;
    fwsec->signatures_size = 0;

    fwsec->ucode_image = bios->data + fwsec_desc_offset + desc_size;
    fwsec->ucode_size = fwsec->raw.v2.imem_load_size + fwsec->raw.v2.dmem_load_size;
  } else if (desc_ver == 3) {
    memcpy(&fwsec->raw.v3, bios->data + fwsec_desc_offset, sizeof(NvFwsecDescV3));

    fwsec->imem_phys_base = fwsec->raw.v3.imem_phys_base;
    fwsec->imem_load_size = fwsec->raw.v3.imem_load_size;
    fwsec->imem_sec_base = 0;
    fwsec->imem_sec_size = 0;

    fwsec->dmem_offset = fwsec->raw.v3.imem_load_size;
    fwsec->dmem_phys_base = fwsec->raw.v3.dmem_phys_base;
    fwsec->dmem_load_size = (fwsec->raw.v3.dmem_load_size + 255U) & ~255U;

    fwsec->interface_offset = fwsec->raw.v3.interface_offset;
    fwsec->pkc_data_offset = fwsec->raw.v3.pkc_data_offset;

    fwsec->ucode_id = fwsec->raw.v3.ucode_id;
    fwsec->signature_count = fwsec->raw.v3.signature_count;
    fwsec->signature_versions = fwsec->raw.v3.signature_versions;
    fwsec->engine_id_mask = fwsec->raw.v3.engine_id_mask;

    if (desc_size > sizeof(NvFwsecDescV3)) {
      fwsec->signatures = bios->data + fwsec_desc_offset + sizeof(NvFwsecDescV3);
      fwsec->signatures_size = desc_size - (uint32_t)sizeof(NvFwsecDescV3);
    } else {
      fwsec->signatures = NULL;
      fwsec->signatures_size = 0;
    }


    fwsec->ucode_image = bios->data + fwsec_desc_offset + desc_size;
    fwsec->ucode_size = fwsec->raw.v3.imem_load_size + fwsec->raw.v3.dmem_load_size;
  } else {
    kprintf("[VBIOS] Error: Unknown FWSEC descriptor version %u\n", desc_ver);
    return -1;
  }

  return 0;
}

int nv_bios_verify_test(const NvDevice *dev) {
  NvBios bios;
  if (nv_bios_init(dev, &bios) != 0) {
    kprintf("[VBIOS/TEST] FAILED: Could not initialize VBIOS from PROM\n");
    return -1;
  }

  kprintf("[VBIOS/TEST] VBIOS dumped successfully (%u KB, BIT at 0x%04x)\n",(uint32_t)(bios.size / 1024), bios.bit_offset);

  NvBitEntry bit_p;
  if (nv_bios_get_bit_entry(&bios, 'p', &bit_p) == 0) {
    kprintf("[VBIOS/TEST] Found BIT 'p': ver=%u len=%u offset=0x%04x\n",(uint32_t)bit_p.version, (uint32_t)bit_p.length, (uint32_t)bit_p.offset);
  } else {
    kprintf("[VBIOS/TEST] FAILED: BIT 'p' token not present\n");
    nv_bios_free(&bios);
    return -1;
  }

  NvFwsecImage fwsec;
  if (nv_bios_extract_fwsec(&bios, &fwsec) == 0) {
    kprintf("[VBIOS/TEST] Extracted FWSEC ucode (type 0x85, desc v%u)\n",(uint32_t)fwsec.version);
    kprintf("IMEM: base=0x%04x size=%u bytes (sec_base=0x%04x sec_size=%u)\n",fwsec.imem_phys_base, fwsec.imem_load_size,fwsec.imem_sec_base, fwsec.imem_sec_size);
    kprintf("DMEM: base=0x%04x size=%u bytes (offset=0x%04x)\n",fwsec.dmem_phys_base, fwsec.dmem_load_size, fwsec.dmem_offset);
    kprintf("Interface Offset: 0x%04x | PKC Offset: 0x%04x\n",fwsec.interface_offset, fwsec.pkc_data_offset);

    if (fwsec.version == 3) {
      kprintf("Ucode ID: %u | Sigs: %u (ver 0x%04x) | Engines: 0x%04x\n",(uint32_t)fwsec.ucode_id, (uint32_t)fwsec.signature_count,(uint32_t)fwsec.signature_versions, (uint32_t)fwsec.engine_id_mask);
    }

    kprintf("Total ucode image payload: %u bytes\n", (uint32_t)fwsec.ucode_size);
  } else {
    kprintf("[VBIOS/TEST] FAILED: Failed to extract FWSEC image\n");
    nv_bios_free(&bios);
    return -1;
  }

  nv_bios_free(&bios);
  kprintf("[VBIOS/TEST] Verification completed successfully.\n");
  return 0;
}

int nv_fwsec_execute_frts(const NvDevice *dev, const NvFwsecImage *fwsec, uint64_t frts_offset) {
  if (!dev || !fwsec || !fwsec->ucode_image || fwsec->ucode_size == 0 || frts_offset == 0) {
    kprintf("[NV/FWSEC] Error: Invalid parameters for FWSEC-FRTS execution\n");
    return -1;
  }

  kprintf("[NV/FWSEC] Preparing FWSEC v%u for FRTS (Offset: 0x%016llx)...\n", fwsec->version, frts_offset);

  FwsecFrtsCmd frts_cmd;
  memset(&frts_cmd, 0, sizeof(FwsecFrtsCmd));

  frts_cmd.readVbiosDesc.version = 1;
  frts_cmd.readVbiosDesc.size = sizeof(FwsecReadVbiosDesc);
  frts_cmd.readVbiosDesc.gfwImageOffset = 0;
  frts_cmd.readVbiosDesc.gfwImageSize = 0;
  frts_cmd.readVbiosDesc.flags = 2; // FWSECLIC_READ_VBIOS_STRUCT_FLAGS

  frts_cmd.frtsRegionDesc.version = 1;
  frts_cmd.frtsRegionDesc.size = sizeof(FwsecFrtsRegionDesc);
  frts_cmd.frtsRegionDesc.frtsRegionOffset4K = (uint32_t)(frts_offset >> 12);
  frts_cmd.frtsRegionDesc.frtsRegionSize = 0x100;
  frts_cmd.frtsRegionDesc.frtsRegionMediaType = 2;

  uint8_t *dmem_buf = (uint8_t *)kmalloc(fwsec->dmem_load_size);
  if (!dmem_buf) {
    kprintf("[NV/FWSEC] Error: Failed to allocate DMEM staging buffer\n");
    return -1;
  }

  memcpy(dmem_buf, fwsec->ucode_image + fwsec->dmem_offset, fwsec->dmem_load_size);

  if (fwsec->interface_offset >= fwsec->dmem_load_size) {
    kprintf("[NV/FWSEC] Error: Invalid interface offset 0x%x\n", fwsec->interface_offset);
    kfree(dmem_buf);
    return -1;
  }

  const FlcnAppIntfHeader *intf_hdr = (const FlcnAppIntfHeader *)(dmem_buf + fwsec->interface_offset);
  uint32_t cur_offset = fwsec->interface_offset + sizeof(FlcnAppIntfHeader);
  FlcnDmemMapperV3 *dmem_mapper = NULL;

  for (uint8_t i = 0; i < intf_hdr->entryCount; i++) {
    if (cur_offset + sizeof(FlcnAppIntfEntry) > fwsec->dmem_load_size) {
      break;
    }
    const FlcnAppIntfEntry *entry = (const FlcnAppIntfEntry *)(dmem_buf + cur_offset);
    cur_offset += sizeof(FlcnAppIntfEntry);

    if (entry->id == FALCON_APPLICATION_INTERFACE_ENTRY_ID_DMEMMAPPER) {
      if (entry->dmemOffset + sizeof(FlcnDmemMapperV3) <= fwsec->dmem_load_size) {
        dmem_mapper = (FlcnDmemMapperV3 *)(dmem_buf + entry->dmemOffset);
        break;
      }
    }
  }

  if (!dmem_mapper) {
    kprintf("[NV/FWSEC] Error: DMEMMAPPER entry not found in interface table\n");
    kfree(dmem_buf);
    return -1;
  }

  dmem_mapper->init_cmd = FALCON_APPLICATION_INTERFACE_DMEM_MAPPER_V3_CMD_FRTS;
  if (dmem_mapper->cmd_in_buffer_offset + sizeof(FwsecFrtsCmd) > fwsec->dmem_load_size) {
    kprintf("[NV/FWSEC] Error: Command input buffer out of bounds\n");
    kfree(dmem_buf);
    return -1;
  }

  memcpy(dmem_buf + dmem_mapper->cmd_in_buffer_offset, &frts_cmd, sizeof(FwsecFrtsCmd));

  NvFalcon gsp_falcon;
  nv_falcon_init_gsp(&gsp_falcon);

  if (nv_falcon_reset(dev, &gsp_falcon) != 0) {
    kprintf("[NV/FWSEC] Error: Failed to reset GSP Falcon engine\n");
    kfree(dmem_buf);
    return -1;
  }

  if (fwsec->imem_load_size > 0) {
    if (nv_falcon_imem_write(dev, &gsp_falcon, fwsec->imem_phys_base,
      fwsec->ucode_image, fwsec->imem_load_size,
      false, fwsec->imem_phys_base) != 0) {
      kprintf("[NV/FWSEC] Error: Failed to write non-secure IMEM to GSP Falcon\n");
    kfree(dmem_buf);
    return -1;
      }
  }

  if (fwsec->imem_sec_size > 0) {
    if (nv_falcon_imem_write(dev, &gsp_falcon, fwsec->imem_sec_base,
      fwsec->ucode_image + fwsec->imem_sec_base,
      fwsec->imem_sec_size,
      true, fwsec->imem_sec_base) != 0) {
      kprintf("[NV/FWSEC] Error: Failed to write secure IMEM to GSP Falcon\n");
    kfree(dmem_buf);
    return -1;
      }
  }

  if (nv_falcon_dmem_write(dev, &gsp_falcon, fwsec->dmem_phys_base,
    dmem_buf, fwsec->dmem_load_size) != 0) {
    kprintf("[NV/FWSEC] Error: Failed to write patched DMEM to GSP Falcon\n");
    kfree(dmem_buf);
    return -1;
  }
  kfree(dmem_buf);

  uint32_t bootvec = (fwsec->version == 2) ? fwsec->raw.v2.virtual_entry : 0x00000000U;
  nv_falcon_set_bootvec(dev, &gsp_falcon, bootvec);

  kprintf("[NV/FWSEC] Starting GSP Falcon CPU to execute FWSEC-FRTS (BOOTVEC=0x%08x)\n", bootvec);

  nv_falcon_start_cpu(dev, &gsp_falcon);

  if (nv_falcon_wait_halt(dev, &gsp_falcon, 2000000U) != 0) {
    uint32_t cpuctl = nv_rd32(dev, gsp_falcon.base_addr + NV_FALCON_CPUCTL);
    kprintf("[NV/FWSEC] Error: FWSEC-FRTS timed out (CPUCTL=0x%08x)\n", cpuctl);
    return -1;
  }

  if (!nv_wpr_is_wpr2_up(dev)) {
    kprintf("[NV/FWSEC] Error: WPR2 aperture not active after FWSEC-FRTS execution\n");
    return -1;
  }

  kprintf("[NV/FWSEC] FWSEC-FRTS completed successfully (PFB_WPR2_HI=0x%08x).\n", nv_rd32(dev, NV_PFB_PRI_MMU_WPR2_ADDR_HI));
  return 0;
}
