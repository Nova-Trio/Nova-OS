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
  uint64_t magic; //0xdc3aae21371a60b3ULL
  uint64_t revision; // 1

  uint64_t sysmemAddrOfRadix3Elf; // addr of radix-3 root
  uint64_t sizeOfRadix3Elf; // .fwimage size

  uint64_t sysmemAddrOfBootloader;
  uint64_t sizeOfBootloader;

  uint64_t bootloaderCodeOffset;
  uint64_t bootloaderDataOffset;
  uint64_t bootloaderManifestOffset;

  union {
    struct {
      uint64_t sysmemAddrOfSignature;
      uint64_t sizeOfSignature;
    };
    struct {
      uint32_t gspFwHeapFreeListWprOffset;
      uint32_t unused0;
      uint64_t unused1;
    };
  };

  uint64_t gspFwRsvdStart;

  uint64_t nonWprHeapOffset;
  uint64_t nonWprHeapSize;

  uint64_t gspFwWprStart;

  uint64_t gspFwHeapOffset;
  uint64_t gspFwHeapSize;

  uint64_t gspFwOffset;
  uint64_t bootBinOffset;

  uint64_t frtsOffset;
  uint64_t frtsSize;

  uint64_t gspFwWprEnd;
  uint64_t fbSize;

  uint64_t vgaWorkspaceOffset;
  uint64_t vgaWorkspaceSize;

  uint64_t bootCount;

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

  uint8_t  gspFwHeapVfPartitionCount;
  uint8_t  flags;
  uint8_t  padding[2];
  uint32_t pmuReservedSize;
  uint64_t verified;
} __attribute__((packed)) GspFwWprMeta;

int nv_wpr_populate_meta(const NvDevice *dev, NvGspContext *gsp, uint64_t fb_size);
void nv_wpr_dump_meta(const GspFwWprMeta *meta);
bool nv_wpr_is_wpr2_up(const NvDevice *dev);
uint64_t nv_fb_get_real_size(const NvDevice *dev);
