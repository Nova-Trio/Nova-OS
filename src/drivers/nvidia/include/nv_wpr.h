#pragma once

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#include <novamod.h>
#include <nv_device.h>
#include <nv_gsp.h>

#define GSP_WPR_ALIGNMENT 0x00020000ULL /* 128 KB alignment requirement */
#define GSP_VBIOS_WORKSPACE_SIZE 0x00020000ULL /* 128 KB reserved at top of FB */
#define GSP_FRTS_SIZE_TU102 0x00100000ULL /* 1 MB Fault Tolerant Runtime State */
#define GSP_WPR_META_SIZE 0x00100000ULL /* 1 MB allocation for WPR metadata header */
#define GSP_NON_WPR_HEAP_SIZE_DEFAULT 0x00800000ULL /* 8 MB Non-WPR Heap */
#define GSP_WPR_HEAP_SIZE_TU10X_DEFAULT 0x04000000ULL /* 64 MB GSP-RM WPR Heap */

#define GSP_ALIGN_UP(x, a)                  (((x) + ((a) - 1ULL)) & ~((a) - 1ULL))
#define GSP_ALIGN_DOWN(x, a)                ((x) & ~((a) - 1ULL))

#define GSP_FW_WPR_META_MAGIC               0xdc3aae21371a60b3ULL
#define GSP_FW_WPR_META_REVISION            1ULL
#define GSP_FW_WPR_META_VERIFIED            0xa0a0a0a0a0a0a0a0ULL
#define GSP_FW_WPR_META_UNVERIFIED          0x0000000000000000ULL

/* GspFwWprMeta flags */
#define GSP_FW_FLAGS_CLOCK_BOOST            (1U << 0)
#define GSP_FW_FLAGS_RECOVERY_MARGIN_PRESENT (1U << 1)
#define GSP_FW_FLAGS_PPCIE_ENABLED          (1U << 2)
#define GSP_FW_FLAGS_MULTI_GPU_NVLE_ENABLED (1U << 3)
#define GSP_FW_FLAGS_SCAN_RECOVERY_MARGIN   (1U << 4)

#define NV_PFB_PRI_MMU_WPR2_ADDR_LO         0x001FA824U
#define NV_PFB_PRI_MMU_WPR2_ADDR_LO_ALIGN   12U
#define NV_PFB_PRI_MMU_WPR2_ADDR_LO_VAL     0xFFFFFFF0U

#define NV_PFB_PRI_MMU_WPR2_ADDR_HI         0x001FA828U
#define NV_PFB_PRI_MMU_WPR2_ADDR_HI_ALIGN   12U
#define NV_PFB_PRI_MMU_WPR2_ADDR_HI_VAL     0xFFFFFFF0U

typedef struct {
  uint64_t magic;                      /* = 0xdc3aae21371a60b3ULL */
  uint64_t revision;                   /* = 1ULL */

  /* ---- Members regarding data in SYSMEM (DMA Addresses) ---- */
  uint64_t sysmemAddrOfRadix3Elf;      /* Host physical address of Radix-3 Level-0 root */
  uint64_t sizeOfRadix3Elf;            /* Size in bytes of .fwimage */

  uint64_t sysmemAddrOfBootloader;     /* Host physical address of bootloader binary */
  uint64_t sizeOfBootloader;           /* Size in bytes of bootloader binary */

  /* Offsets inside bootloader image (from RM_RISCV_UCODE_DESC) */
  uint64_t bootloaderCodeOffset;       /* monitorCodeOffset (0 on Turing v4) */
  uint64_t bootloaderDataOffset;       /* monitorDataOffset (0 on Turing v4) */
  uint64_t bootloaderManifestOffset;   /* manifestOffset (0 on Turing v4) */

  union {
    struct {
      uint64_t sysmemAddrOfSignature; /* Host physical address of RSA-3072 signature */
      uint64_t sizeOfSignature;       /* Size in bytes of signature buffer */
    };
    struct {
      uint32_t gspFwHeapFreeListWprOffset;
      uint32_t unused0;
      uint64_t unused1;
    };
  };

  /* ---- Members describing VRAM Carveout Layout ---- */
  uint64_t gspFwRsvdStart;             /* Identical to nonWprHeapOffset */

  uint64_t nonWprHeapOffset;           /* Base offset in FB of Non-WPR Heap */
  uint64_t nonWprHeapSize;             /* Size of Non-WPR Heap (8 MB) */

  uint64_t gspFwWprStart;              /* Base offset in FB of WPR2 region (holds WPR metadata) */

  uint64_t gspFwHeapOffset;            /* Base offset in FB of GSP-RM WPR Heap */
  uint64_t gspFwHeapSize;              /* Size of GSP-RM WPR Heap (64 MB) */

  uint64_t gspFwOffset;                /* Target offset in FB for .fwimage ELF */
  uint64_t bootBinOffset;              /* Target offset in FB for bootloader binary */

  uint64_t frtsOffset;                 /* Target offset in FB for FRTS data */
  uint64_t frtsSize;                   /* Size of FRTS data (1 MB) */

  uint64_t gspFwWprEnd;                /* End offset in FB of WPR2 region (128 KB aligned) */
  uint64_t fbSize;                     /* Total usable VRAM size in bytes */

  uint64_t vgaWorkspaceOffset;         /* Base offset of VBIOS VGA workspace */
  uint64_t vgaWorkspaceSize;           /* Size of VBIOS VGA workspace (128 KB) */

  uint64_t bootCount;                  /* Boot count (0 on initial boot) */

  union {
    struct {
      uint64_t partitionRpcAddr;
      uint16_t partitionRpcRequestOffset;
      uint16_t partitionRpcReplyOffset;
      uint32_t elfCodeOffset;
      uint32_t elfDataOffset;
      uint32_t elfCodeSize;
      uint32_t elfDataSize;
      uint32_t lsUcodeVersion;
    } partition;
    struct {
      uint32_t partitionRpcPadding[4];
      uint64_t sysmemAddrOfCrashReportQueue;
      uint32_t sizeOfCrashReportQueue;
      uint32_t lsUcodeVersionPadding[1];
    } crashcat;
  };

  uint8_t  gspFwHeapVfPartitionCount;  /* VF partition count (0 for baremetal) */
  uint8_t  flags;                      /* Flags (e.g. GSP_FW_FLAGS_CLOCK_BOOST) */
  uint8_t  padding[2];
  uint32_t pmuReservedSize;            /* PMU reserved size (0 on Turing) */
  uint64_t verified;                   /* Set to GSP_FW_WPR_META_UNVERIFIED by CPU, VERIFIED by SEC2 */
} __attribute__((packed)) GspFwWprMeta;

_Static_assert(sizeof(GspFwWprMeta) == 256, "GspFwWprMeta layout must be exactly 256 bytes");

int nv_wpr_populate_meta(const NvDevice *dev, NvGspContext *gsp, uint64_t fb_size);
void nv_wpr_dump_meta(const GspFwWprMeta *meta);
bool nv_wpr_is_wpr2_up(const NvDevice *dev);
uint64_t nv_fb_get_real_size(const NvDevice *dev);
