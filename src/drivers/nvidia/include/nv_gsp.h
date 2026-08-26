#pragma once

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#include <novamod.h>
#include <nv_device.h>
#include <nv_dma.h>


#define GSP_PAGE_SHIFT 12ULL
#define GSP_PAGE_SIZE (1ULL << GSP_PAGE_SHIFT) // 4096 bytes
#define GSP_RADIX3_ENTRIES_PER_PAGE (GSP_PAGE_SIZE / sizeof(uint64_t)) // 512

// LibOS staging buffer sizes
#define GSP_LOG_BUFFER_SIZE 0x10000ULL // 64 KB (16 pages)
#define GSP_RMARGS_SIZE 0x01000ULL // 4 KB (1 page)
#define GSP_LIBOS_ARGS_SIZE 0x01000ULL // 4 KB (1 page)

// Shared memory queue geometry
#define GSP_SHM_CMDQ_SIZE 0x40000ULL // 256 KB
#define GSP_SHM_MSGQ_SIZE 0x40000ULL // 256 KB
#define GSP_SHM_PTE_PAGES 1ULL // 1 page (holds 129 PTEs)
#define GSP_SHM_TOTAL_PAGES 129ULL // 1 PTE page + 64 cmdq + 64 msgq
#define GSP_SHM_TOTAL_SIZE (GSP_SHM_TOTAL_PAGES * GSP_PAGE_SIZE) // 528448 bytes

#define GSP_SHM_CMDQ_OFFSET 0x01000ULL
#define GSP_SHM_MSGQ_OFFSET 0x41000ULL


#define LIBOS_ID_LOGINIT 0x004C4F47494E4954ULL // "LOGINIT"
#define LIBOS_ID_LOGINTR 0x004C4F47494E5452ULL // "LOGINTR"
#define LIBOS_ID_LOGRM 0x0000004C4F47524DULL // "LOGRM"
#define LIBOS_ID_RMARGS 0x0000524D41524753ULL // "RMARGS"

static inline uint64_t nv_gsp_libos_id8(const char *name) {
  uint64_t id = 0;
  for (size_t i = 0; i < sizeof(id) && name[i]; i++) {
    id = (id << 8) | (uint8_t)name[i];
  }
  return id;
}

#define LIBOS_MEMORY_REGION_CONTIGUOUS 1U
#define LIBOS_MEMORY_REGION_LOC_SYSMEM 1U


#define GSP_FW_WPR_META_MAGIC 0xdc3aae21371a60b3ULL
#define GSP_FW_WPR_META_REVISION 1ULL
#define GSP_FW_WPR_META_VERIFIED 0xa0a0a0a0a0a0a0a0ULL
#define GSP_FW_WPR_META_UNVERIFIED 0x0000000000000000ULL

#define GSP_FW_SIGNATURE_SECTION_TU11X ".fwsignature_tu11x"
#define GSP_FW_SIGNATURE_SECTION_TU10X ".fwsignature_tu10x"
#define GSP_FW_IMAGE_SECTION ".fwimage"

#define EI_NIDENT 16
#define ELFMAG "\177ELF"
#define SELFMAG 4
#define ELFCLASS64 2

typedef struct {
  uint8_t  e_ident[EI_NIDENT];
  uint16_t e_type;
  uint16_t e_machine;
  uint32_t e_version;
  uint64_t e_entry;
  uint64_t e_phoff;
  uint64_t e_shoff;
  uint32_t e_flags;
  uint16_t e_ehsize;
  uint16_t e_phentsize;
  uint16_t e_phnum;
  uint16_t e_shentsize;
  uint16_t e_shnum;
  uint16_t e_shstrndx;
} __attribute__((packed)) Elf64_Ehdr;

typedef struct {
  uint32_t sh_name;
  uint32_t sh_type;
  uint64_t sh_flags;
  uint64_t sh_addr;
  uint64_t sh_offset;
  uint64_t sh_size;
  uint32_t sh_link;
  uint32_t sh_info;
  uint64_t sh_addralign;
  uint64_t sh_entsize;
} __attribute__((packed)) Elf64_Shdr;


#define NV_VGPU_MSG_FUNCTION_CONTINUATION_RECORD 71U
#define NV_VGPU_MSG_FUNCTION_GSP_SET_SYSTEM_INFO 72U
#define NV_VGPU_MSG_FUNCTION_SET_REGISTRY 73U

#define NV_VGPU_MSG_EVENT_GSP_INIT_DONE 0x1001U
#define NV_VGPU_MSG_EVENT_GSP_RUN_CPU_SEQUENCER 0x1002U

#define GSP_RPC_HEADER_VERSION 0x03000000U
#define GSP_RPC_SIGNATURE 0x43505256U // 'C'<<24 | 'P'<<16 | 'R'<<8 | 'V'

#define GSP_REGISTRY_TYPE_DWORD 1U
#define GSP_REGISTRY_TYPE_BINARY 2U
#define GSP_REGISTRY_TYPE_STRING 3U


typedef struct {
  uint8_t auth_tag[16];
  uint8_t aad[16];
  uint32_t checksum;
  uint32_t sequence;
  uint32_t elem_count;
  uint32_t pad;
} __attribute__((packed)) NvGspMsgElemHdr;
_Static_assert(sizeof(NvGspMsgElemHdr) == 48, "NvGspMsgElemHdr must be 48 bytes");

typedef struct {
  uint32_t header_version;
  uint32_t signature;
  uint32_t length;
  uint32_t function;
  uint32_t rpc_result;
  uint32_t rpc_result_private;
  uint32_t sequence;
  uint32_t cpu_rm_gfid;
} __attribute__((packed)) NvGspRpcHdr;
_Static_assert(sizeof(NvGspRpcHdr) == 32, "NvGspRpcHdr must be 32 bytes");


typedef struct {
  uint16_t device_id;
  uint16_t vendor_id;
  uint16_t subdevice_id;
  uint16_t subvendor_id;
  uint8_t revision_id;
} __attribute__((packed)) NvGspBusInfo;

typedef struct {
  uint32_t status;
  uint32_t acpi_id_list_len;
  uint32_t acpi_id_list[16];
} NvGspDodMethodData;

typedef struct {
  uint32_t status;
  uint32_t jt_caps;
  uint16_t jt_rev_id;
  uint8_t b_sbios_caps;
} NvGspJtMethodData;

typedef struct {
  uint32_t acpi_id;
  uint32_t mode;
  uint32_t status;
} NvGspMuxMethodDataElement;

typedef struct {
  uint32_t table_len;
  NvGspMuxMethodDataElement acpi_id_mux_mode_table[16];
  NvGspMuxMethodDataElement acpi_id_mux_part_table[16];
} NvGspMuxMethodData;

typedef struct {
  uint32_t status;
  uint32_t optimus_caps;
} NvGspCapsMethodData;

typedef struct {
  uint8_t b_valid;
  NvGspDodMethodData dod_method_data;
  NvGspJtMethodData jt_method_data;
  NvGspMuxMethodData mux_method_data;
  NvGspCapsMethodData caps_method_data;
} NvGspAcpiMethodData;

typedef struct {
  uint32_t total_vfs;
  uint32_t first_vf_offset;
  uint64_t first_vf_bar0_address;
  uint64_t first_vf_bar1_address;
  uint64_t first_vf_bar2_address;
  uint8_t b_64bit_bar0;
  uint8_t b_64bit_bar1;
  uint8_t b_64bit_bar2;
} NvGspVfInfo;

typedef struct {
  uint64_t gpu_phys_addr;
  uint64_t gpu_phys_fb_addr;
  uint64_t gpu_phys_inst_addr;
  uint64_t nv_domain_bus_device_func;
  uint64_t sim_access_buf_phys_addr;
  uint64_t pcie_atomics_op_mask;
  uint64_t console_mem_size;
  uint64_t max_user_va;
  uint32_t pci_config_mirror_base;
  uint32_t pci_config_mirror_size;
  uint8_t oor_arch;
  uint64_t cl_pdb_properties;
  uint32_t chipset;
  uint8_t b_gpu_behind_bridge;
  uint8_t b_mnoc_available;
  uint8_t b_upstream_l0s_unsupported;
  uint8_t b_upstream_l1_unsupported;
  uint8_t b_upstream_l1_por_supported;
  uint8_t b_upstream_l1_por_mobile_only;
  uint8_t upstream_address_valid;
  NvGspBusInfo fhb_bus_info;
  NvGspBusInfo chipset_id_info;
  NvGspAcpiMethodData acpi_method_data;
  uint32_t hypervisor_type;
  uint8_t b_is_passthru;
  uint64_t sys_timer_offset_ns;
  NvGspVfInfo gsp_vf_info;
} NvGspSystemInfoPayload;

typedef struct {
  uint32_t name_offset;
  uint8_t type;
  uint32_t data;
  uint32_t length;
} NvGspPackedRegEntry;
_Static_assert(sizeof(NvGspPackedRegEntry) == 16, "NvGspPackedRegEntry must be 16 bytes");

typedef struct {
  uint32_t size;
  uint32_t num_entries;
  NvGspPackedRegEntry entries[];
} PACKED_REGISTRY_TABLE_HDR;

typedef struct {
  uint32_t version;
  uint32_t bootloaderOffset;
  uint32_t bootloaderSize;
  uint32_t bootloaderParamOffset;
  uint32_t bootloaderParamSize;
  uint32_t riscvElfOffset;
  uint32_t riscvElfSize;
  uint32_t appVersion;
  uint32_t manifestOffset;
  uint32_t manifestSize;
  uint32_t monitorDataOffset;
  uint32_t monitorDataSize;
  uint32_t monitorCodeOffset;
  uint32_t monitorCodeSize;
  uint32_t bIsMonitorEnabled;
  uint32_t swbromCodeOffset;
  uint32_t swbromCodeSize;
  uint32_t swbromDataOffset;
  uint32_t swbromDataSize;
  uint32_t fbReservedSize;
  uint32_t bSignedAsCode;
  uint32_t bIsSmp;
  uint32_t bIsPlicEnabled;
} __attribute__((packed)) NvGspBootloaderDesc;
_Static_assert(sizeof(NvGspBootloaderDesc) == 92, "NvGspBootloaderDesc must be 92 bytes");

typedef struct {
  uint32_t bin_magic;
  uint32_t bin_ver;
  uint32_t bin_size;
  uint32_t header_offset;
  uint32_t data_offset;
  uint32_t data_size;
} __attribute__((packed)) NvGspBinHdr;

// LibOS memory region argument
typedef struct {
  uint64_t id8; // ASCII Identifier
  uint64_t pa; // 64-bit physical host DMA address
  uint64_t size; // Size of region in bytes
  uint8_t kind; // LIBOS_MEMORY_REGION_CONTIGUOUS
  uint8_t loc; // LIBOS_MEMORY_REGION_LOC_SYSMEM
  uint8_t padding[6];
} __attribute__((packed)) LibosMemoryRegionInitArgument;

// Cached RM boot args (RMARGS)
typedef struct {
  uint64_t sharedMemPhysAddr;
  uint32_t pageTableEntryCount;
  uint32_t reserved;
  uint64_t cmdQueueOffset;
  uint64_t statQueueOffset;
  uint64_t locklessCmdQueueOffset;
  uint64_t locklessStatQueueOffset;
} __attribute__((packed)) NvGspMsgqInitArgs;
_Static_assert(sizeof(NvGspMsgqInitArgs) == 48, "NvGspMsgqInitArgs layout must be 48 bytes");

typedef struct {
  uint32_t oldLevel;
  uint32_t flags;
  uint32_t bInPMTransition;
} __attribute__((packed)) NvGspSrInitArgs;

typedef struct {
  NvGspMsgqInitArgs messageQueueInitArguments;
  NvGspSrInitArgs srInitArguments;
  uint32_t gpuInstance;
  struct {
    uint64_t pa;
    uint64_t size;
  } profilerArgs;
} __attribute__((packed)) GspArgumentsCached;
_Static_assert(sizeof(GspArgumentsCached) == 80, "GspArgumentsCached layout must be 80 bytes");

// Message queue ring buffer headers
typedef struct {
  uint32_t version; // Queue version (0)
  uint32_t size; // Total queue size in bytes (256 KB)
  uint32_t msgSize; // Slot size (4096 bytes)
  uint32_t msgCount; // Number of slots in queue (63)
  uint32_t writePtr; // Index of next write slot
  uint32_t flags; // Swap RX flag (1)
  uint32_t rxHdrOff; // Offset of msgqRxHeader in backing store
  uint32_t entryOff; // Offset of data slots from queue base (4096 bytes)
} NvGspMsgqTxHeader;
_Static_assert(sizeof(NvGspMsgqTxHeader) == 32, "NvGspMsgqTxHeader must be 32 bytes");


typedef struct {
  uint32_t readPtr;
} NvGspMsgqRxHeader;
_Static_assert(sizeof(NvGspMsgqRxHeader) == 4, "NvGspMsgqRxHeader must be 4 bytes");

typedef struct {
  NvDmaBuffer lvl0; // 4KB holds 1 ptr to lvl1
  NvDmaBuffer lvl1; // 4KB holds up to 512 ptrs to lvl2
  NvDmaBuffer lvl2; // lvl2 pages * 4KB

  size_t fw_size; // .fwimage
  size_t fw_page_count;
  uint64_t *fw_page_phys;
  void *fw_raw_buffer;
} NvGspRadix3;

typedef struct {
  NvDmaBuffer sig;
  NvDmaBuffer boot_fw;
  NvGspBootloaderDesc boot_desc;

  NvGspRadix3 radix3;

  NvDmaBuffer loginit;
  NvDmaBuffer logintr;
  NvDmaBuffer logrm;
  NvDmaBuffer rmargs;
  NvDmaBuffer libos;

  NvDmaBuffer shm;

  volatile uint32_t *cmdq_wptr;
  volatile uint32_t *cmdq_rptr;
  volatile uint32_t *msgq_wptr;
  volatile uint32_t *msgq_rptr;
  uint32_t cmdq_seq;

  NvDmaBuffer wpr_meta;
} NvGspContext;

int  nv_gsp_fw_stage_all(const NvDevice *dev, NvGspContext *gsp);
void nv_gsp_fw_cleanup(NvGspContext *gsp);

__attribute__((warning("Verify functions are empty on master branch")))
int  nv_gsp_fw_verify_test(const NvDevice *dev, const NvGspContext *gsp);

int  nv_gsp_push_preinit_rpc(const NvDevice *dev, NvGspContext *gsp, uint32_t fn, const void *payload, size_t payload_size);
int  nv_gsp_submit_preinit_sequence(const NvDevice *dev, NvGspContext *gsp);
