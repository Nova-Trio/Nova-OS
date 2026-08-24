#pragma once
#include <novamod.h>
#include <nv_device.h>

#define NV_PROM_BASE 0x00300000U

#define PCI_ROM_SIGNATURE 0xAA55U
#define PCI_ROM_SIGNATURE_NV 0x4E56U // "NV"
#define PCI_ROM_SIGNATURE_NV2 0xBB77U
#define IS_VALID_PCI_ROM_SIG(sig) ((sig) == PCI_ROM_SIGNATURE || (sig) == PCI_ROM_SIGNATURE_NV || (sig) == PCI_ROM_SIGNATURE_NV2)

#define PCI_DATA_STRUCT_SIG 0x52494350U // "PCIR"
#define PCI_DATA_STRUCT_SIG_NV 0x5344504EU // "NPDS"
#define PCI_DATA_STRUCT_SIG_NV2 0x53494752U // "RGIS"
#define IS_VALID_PCI_DATA_SIG(sig) ((sig) == PCI_DATA_STRUCT_SIG || (sig) == PCI_DATA_STRUCT_SIG_NV || (sig) == PCI_DATA_STRUCT_SIG_NV2)

#define NV_PCI_DATA_EXT_SIG 0x4544504EU // "NPDE"
#define NV_ROM_DIRECTORY_IDENTIFIER 0x44524652U // "RFRD"
#define NV_PBUS_IFR_FMT_FIXED0_SIG 0x4E564946U // "NVIF"

#define PCI_ROM_IMAGE_BLOCK_SIZE 512U
#define PCI_LAST_IMAGE_FLAG 0x80U

#define NV_PMU_UCODE_TYPE_FWSEC 0x85U

typedef struct {
  uint8_t *data;
  size_t size;
  uint32_t bit_offset;
} NvBios;

typedef struct {
  uint8_t id;
  uint8_t version;
  uint16_t length;
  uint16_t offset;
} NvBitEntry;

typedef struct {
  uint32_t hdr;
  uint32_t stored_size;
  uint32_t uncompressed_size;
  uint32_t virtual_entry;
  uint32_t interface_offset;
  uint32_t imem_phys_base;
  uint32_t imem_load_size;
  uint32_t imem_virt_base;
  uint32_t imem_sec_base;
  uint32_t imem_sec_size;
  uint32_t dmem_offset;
  uint32_t dmem_phys_base;
  uint32_t dmem_load_size;
  uint32_t alt_imem_load_size;
  uint32_t alt_dmem_load_size;
} __attribute__((packed)) NvFwsecDescV2;

typedef struct {
  uint32_t hdr;
  uint32_t stored_size;
  uint32_t pkc_data_offset;
  uint32_t interface_offset;
  uint32_t imem_phys_base;
  uint32_t imem_load_size;
  uint32_t imem_virt_base;
  uint32_t dmem_phys_base;
  uint32_t dmem_load_size;
  uint16_t engine_id_mask;
  uint8_t ucode_id;
  uint8_t signature_count;
  uint16_t signature_versions;
  uint16_t reserved;
} __attribute__((packed)) NvFwsecDescV3;

typedef struct {
  uint8_t  version; // 2 or 3
  union {
    NvFwsecDescV2 v2;
    NvFwsecDescV3 v3;
  } raw;

  uint32_t imem_phys_base;
  uint32_t imem_load_size;
  uint32_t imem_sec_base;
  uint32_t imem_sec_size;

  uint32_t dmem_offset;
  uint32_t dmem_phys_base;
  uint32_t dmem_load_size;

  uint32_t interface_offset;
  uint32_t pkc_data_offset;

  uint8_t ucode_id;
  uint8_t signature_count;
  uint16_t signature_versions;
  uint16_t engine_id_mask;

  const uint8_t *signatures;
  uint32_t signatures_size;

  const uint8_t *ucode_image;
  size_t ucode_size;
} NvFwsecImage;

int nv_bios_init(const NvDevice *dev, NvBios *bios);
void nv_bios_free(NvBios *bios);

int nv_bios_get_bit_entry(const NvBios *bios, uint8_t id, NvBitEntry *entry);
int nv_bios_extract_fwsec(const NvBios *bios, NvFwsecImage *fwsec);

int nv_bios_verify_test(const NvDevice *dev);
