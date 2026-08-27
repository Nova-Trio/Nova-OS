#pragma once

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#include <novamod.h>
#include <nv_device.h>
#include <nv_gsp.h>

#define GSP_MSG_MIN_SIZE GSP_PAGE_SIZE
#define GSP_MSG_MAX_SIZE (GSP_MSG_MIN_SIZE * 16ULL) // 64 KB

#define NV_GSP_MAX_MSG_NTFY 16U
#define NV_GSP_RPC_DEFAULT_TIMEOUT_US 4000000U // 4sec

#define GSP_MSG_HDR_SIZE (sizeof(NvGspMsgElemHdr))

#ifndef NV_VGPU_MSG_FUNCTION_NOP
#define NV_VGPU_MSG_FUNCTION_NOP 0U
#endif
#ifndef NV_VGPU_MSG_FUNCTION_SET_GUEST_SYSTEM_INFO
#define NV_VGPU_MSG_FUNCTION_SET_GUEST_SYSTEM_INFO 1U
#endif
#ifndef NV_VGPU_MSG_FUNCTION_ALLOC_ROOT
#define NV_VGPU_MSG_FUNCTION_ALLOC_ROOT 2U
#endif
#ifndef NV_VGPU_MSG_FUNCTION_ALLOC_DEVICE
#define NV_VGPU_MSG_FUNCTION_ALLOC_DEVICE 3U
#endif
#ifndef NV_VGPU_MSG_FUNCTION_ALLOC_MEMORY
#define NV_VGPU_MSG_FUNCTION_ALLOC_MEMORY 4U
#endif
#ifndef NV_VGPU_MSG_FUNCTION_ALLOC_CTX_DMA
#define NV_VGPU_MSG_FUNCTION_ALLOC_CTX_DMA 5U
#endif
#ifndef NV_VGPU_MSG_FUNCTION_ALLOC_CHANNEL_DMA
#define NV_VGPU_MSG_FUNCTION_ALLOC_CHANNEL_DMA 6U
#endif
#ifndef NV_VGPU_MSG_FUNCTION_MAP_MEMORY
#define NV_VGPU_MSG_FUNCTION_MAP_MEMORY 7U
#endif
#ifndef NV_VGPU_MSG_FUNCTION_ALLOC_OBJECT
#define NV_VGPU_MSG_FUNCTION_ALLOC_OBJECT 9U
#endif
#ifndef NV_VGPU_MSG_FUNCTION_FREE
#define NV_VGPU_MSG_FUNCTION_FREE 10U
#endif
#ifndef NV_VGPU_MSG_FUNCTION_LOG
#define NV_VGPU_MSG_FUNCTION_LOG 11U
#endif
#ifndef NV_VGPU_MSG_FUNCTION_ALLOC_VIDMEM
#define NV_VGPU_MSG_FUNCTION_ALLOC_VIDMEM 12U
#endif
#ifndef NV_VGPU_MSG_FUNCTION_UNMAP_MEMORY
#define NV_VGPU_MSG_FUNCTION_UNMAP_MEMORY 13U
#endif
#ifndef NV_VGPU_MSG_FUNCTION_GET_EDID
#define NV_VGPU_MSG_FUNCTION_GET_EDID 16U
#endif
#ifndef NV_VGPU_MSG_FUNCTION_ALLOC_SUBDEVICE
#define NV_VGPU_MSG_FUNCTION_ALLOC_SUBDEVICE 19U
#endif
#ifndef NV_VGPU_MSG_FUNCTION_UNLOADING_GUEST_DRIVER
#define NV_VGPU_MSG_FUNCTION_UNLOADING_GUEST_DRIVER 47U
#endif
#ifndef NV_VGPU_MSG_FUNCTION_TDR_SET_TIMEOUT_STATE
#define NV_VGPU_MSG_FUNCTION_TDR_SET_TIMEOUT_STATE 48U
#endif
#ifndef NV_VGPU_MSG_FUNCTION_GET_STATIC_INFO
#define NV_VGPU_MSG_FUNCTION_GET_STATIC_INFO 51U
#endif
#ifndef NV_VGPU_MSG_FUNCTION_GET_ENCODER_CAPACITY
#define NV_VGPU_MSG_FUNCTION_GET_ENCODER_CAPACITY 62U
#endif
#ifndef NV_VGPU_MSG_FUNCTION_GET_GSP_STATIC_INFO
#define NV_VGPU_MSG_FUNCTION_GET_GSP_STATIC_INFO 65U
#endif
#ifndef NV_VGPU_MSG_FUNCTION_GSP_RM_CONTROL
#define NV_VGPU_MSG_FUNCTION_GSP_RM_CONTROL 76U
#endif
#ifndef NV_VGPU_MSG_FUNCTION_CTRL_GPU_HANDLE_VF_PRI_FAULT
#define NV_VGPU_MSG_FUNCTION_CTRL_GPU_HANDLE_VF_PRI_FAULT 90U
#endif
#ifndef NV_VGPU_MSG_FUNCTION_CTRL_GPFIFO_SCHEDULE
#define NV_VGPU_MSG_FUNCTION_CTRL_GPFIFO_SCHEDULE 97U
#endif
#ifndef NV_VGPU_MSG_FUNCTION_GSP_RM_ALLOC
#define NV_VGPU_MSG_FUNCTION_GSP_RM_ALLOC 103U
#endif
#ifndef NV_VGPU_MSG_FUNCTION_CTRL_GPU_PROMOTE_CTX
#define NV_VGPU_MSG_FUNCTION_CTRL_GPU_PROMOTE_CTX 111U
#endif
#ifndef NV_VGPU_MSG_FUNCTION_CTRL_GPU_INITIALIZE_CTX
#define NV_VGPU_MSG_FUNCTION_CTRL_GPU_INITIALIZE_CTX 115U
#endif
#ifndef NV_VGPU_MSG_FUNCTION_CTRL_GPU_EVICT_CTX
#define NV_VGPU_MSG_FUNCTION_CTRL_GPU_EVICT_CTX 146U
#endif

#ifndef NV_VGPU_MSG_EVENT_FIRST_EVENT
#define NV_VGPU_MSG_EVENT_FIRST_EVENT 0x1000U
#endif
#ifndef NV_VGPU_MSG_EVENT_POST_EVENT
#define NV_VGPU_MSG_EVENT_POST_EVENT 0x1003U
#endif
#ifndef NV_VGPU_MSG_EVENT_RC_TRIGGERED
#define NV_VGPU_MSG_EVENT_RC_TRIGGERED 0x1004U
#endif
#ifndef NV_VGPU_MSG_EVENT_MMU_FAULT_QUEUED
#define NV_VGPU_MSG_EVENT_MMU_FAULT_QUEUED 0x1005U
#endif
#ifndef NV_VGPU_MSG_EVENT_OS_ERROR_LOG
#define NV_VGPU_MSG_EVENT_OS_ERROR_LOG 0x1006U
#endif
#ifndef NV_VGPU_MSG_EVENT_UCODE_LIBOS_PRINT
#define NV_VGPU_MSG_EVENT_UCODE_LIBOS_PRINT 0x100CU
#endif
#ifndef NV_VGPU_MSG_EVENT_PERF_BRIDGELESS_INFO_UPDATE
#define NV_VGPU_MSG_EVENT_PERF_BRIDGELESS_INFO_UPDATE 0x100FU
#endif
#ifndef NV_VGPU_MSG_EVENT_DISPLAY_MODESET
#define NV_VGPU_MSG_EVENT_DISPLAY_MODESET 0x1011U
#endif
#ifndef NV_VGPU_MSG_EVENT_GSP_SEND_USER_SHARED_DATA
#define NV_VGPU_MSG_EVENT_GSP_SEND_USER_SHARED_DATA 0x101BU
#endif

#define NV0080_CTRL_GR_CAPS_TBL_SIZE 23U
#define NV2080_GPU_MAX_GID_LENGTH 256U
#define NV2080_GPU_MAX_NAME_STRING_LENGTH 64U

#define NV2080_CTRL_CMD_FB_GET_FB_REGION_INFO_MAX_ENTRIES 16U
#define NV2080_CTRL_CMD_FB_GET_FB_REGION_INFO_MEM_TYPES 17U
#define NV2080_CTRL_CMD_GR_CTXSW_PREEMPTION_BIND_BUFFERS_CONTEXT_POOL 5U

typedef enum {
  NV_GSP_RPC_REPLY_NOWAIT,
  NV_GSP_RPC_REPLY_NOSEQ,
  NV_GSP_RPC_REPLY_RECV,
  NV_GSP_RPC_REPLY_POLL,
} NvGspRpcReplyPolicy;

typedef struct {
  uint32_t hClient;
  uint32_t hParent;
  uint32_t hObject;
  uint32_t hClass;
  uint32_t status;
  uint32_t paramsSize;
  uint32_t flags;
  uint8_t reserved[4];
  uint8_t params[];
} __attribute__((packed)) rpc_gsp_rm_alloc_v03_00;

typedef struct {
  uint32_t hClient;
  uint32_t hObject;
  uint32_t cmd;
  uint32_t status;
  uint32_t paramsSize;
  uint32_t flags;
  uint8_t params[];
} __attribute__((packed)) rpc_gsp_rm_control_v03_00;

_Static_assert(sizeof(rpc_gsp_rm_control_v03_00) == 24, "rpc_gsp_rm_control_v03_00 must be 24 bytes");

typedef struct {
  uint32_t hRoot;
  uint32_t hObjectParent;
  uint32_t hObjectOld;
  int32_t status;
} __attribute__((packed)) NVOS00_PARAMETERS_v03_00;

typedef struct {
  NVOS00_PARAMETERS_v03_00 params;
} __attribute__((packed)) rpc_free_v03_00;

typedef struct {
  uint8_t bInPMTransition;
  uint8_t bGc6Entering;
  uint8_t pad[2];
  uint32_t newLevel;
} __attribute__((packed)) rpc_unloading_guest_driver_v1F_07;

typedef struct {
  uint32_t exceptType;
  uint32_t runlistId;
  uint32_t chid;
  char errString[256];
} __attribute__((packed)) rpc_os_error_log_v17_00;

typedef struct {
  uint32_t nv2080EngineType;
  uint32_t chid;
  uint32_t exceptType;
  uint32_t scope;
  uint16_t partitionAttributionId;
  uint8_t pad[2];
} __attribute__((packed)) rpc_rc_triggered_v17_02;

typedef struct {
  uint32_t hClient;
  uint32_t hEvent;
  uint32_t notifyIndex;
  uint32_t data;
  uint16_t info16;
  uint8_t pad0[2];
  uint32_t status;
  uint32_t eventDataSize;
  uint8_t bNotifyList;
  uint8_t pad1[3];
  uint8_t eventData[];
} __attribute__((packed)) rpc_post_event_v17_00;

#define to_gsp_msg_hdr(payload_ptr) ((NvGspMsgElemHdr *)((uint8_t *)(payload_ptr) - sizeof(NvGspRpcHdr) - sizeof(NvGspMsgElemHdr)))

#define to_gsp_rpc_hdr(payload_ptr) ((NvGspRpcHdr *)((uint8_t *)(payload_ptr) - sizeof(NvGspRpcHdr)))

#define to_alloc_hdr(params_ptr) ((rpc_gsp_rm_alloc_v03_00 *)((uint8_t *)(params_ptr) - offsetof(rpc_gsp_rm_alloc_v03_00, params)))

#define to_ctrl_hdr(params_ptr) ((rpc_gsp_rm_control_v03_00 *)((uint8_t *)(params_ptr) - offsetof(rpc_gsp_rm_control_v03_00, params)))

#define NV0080_CTRL_GR_CAPS_TBL_SIZE 23U
#define NV2080_GPU_MAX_GID_LENGTH 256U
#define MAX_GPC_COUNT 32U
#define NV2080_GPU_MAX_NAME_STRING_LENGTH 64U
#define NV2080_CTRL_CMD_FB_GET_FB_REGION_INFO_MAX_ENTRIES 16U
#define NV2080_CTRL_CMD_FB_GET_FB_REGION_INFO_MEM_TYPES 17U
#define NV2080_CTRL_CMD_GR_CTXSW_PREEMPTION_BIND_BUFFERS_CONTEXT_POOL 5U
#define RM_ENGINE_TYPE_LAST 0x3EU
#define NVGPU_ENGINE_CAPS_MASK_BITS 32U
#define NVGPU_ENGINE_CAPS_MASK_ARRAY_MAX (((RM_ENGINE_TYPE_LAST - 1U) / NVGPU_ENGINE_CAPS_MASK_BITS) + 1U)

#define NV2080_CTRL_CMD_INTERNAL_INTR_GET_KERNEL_TABLE (0x20800a5c)
#define NV2080_CTRL_INTERNAL_INTR_MAX_TABLE_SIZE 128U
#define NV2080_INTR_CATEGORY_ENUM_COUNT 7U

#define NV01_ROOT_CLIENT 0x00000041U
#define NV01_DEVICE_0 0x00000080U
#define NV20_SUBDEVICE_0 0x00002080U
#define FERMI_VASPACE_A 0x000090F1U
#define AMPERE_CHANNEL_GPFIFO_A 0x0000C56FU
#define TURING_DMA_COPY_A 0x0000C5B5U

#define NV_RM_HANDLE_CLIENT 0xCAFE0001U
#define NV_RM_HANDLE_DEVICE 0xCAFE0080U
#define NV_RM_HANDLE_SUBDEVICE 0xCAFE2080U
#define NV_RM_HANDLE_VASPACE 0xCAFE90F1U

#define NV0080_CTRL_CMD_GPU_GET_CLASSLIST 0x00800201U
#define NV0080_CTRL_GPU_CLASSLIST_MAX_SIZE 256U
typedef struct {
  uint32_t numClasses;
  uint32_t classList[NV0080_CTRL_GPU_CLASSLIST_MAX_SIZE];
} NV0080_CTRL_GPU_GET_CLASSLIST_PARAMS;

#define NV0080_CTRL_CMD_GPU_GET_NUM_SUBDEVICES  0x00800280U

typedef struct {
  uint32_t numSubDevices;
} NV0080_CTRL_GPU_GET_NUM_SUBDEVICES_PARAMS;
_Static_assert(sizeof(NV0080_CTRL_GPU_GET_NUM_SUBDEVICES_PARAMS) == 4, "Must be 4 bytes");

#define NV_PROC_NAME_MAX_LENGTH 100U
typedef struct {
  uint32_t hClient;
  uint32_t processID;
  char processName[NV_PROC_NAME_MAX_LENGTH];
  uint64_t pOsPidInfo __attribute__((aligned(8)));
} __attribute__((packed, aligned(8))) NV0000_ALLOC_PARAMETERS;

#define NV_DEVICE_ALLOCATION_VAMODE_MULTIPLE_VASPACES 0x00000002U
typedef struct {
  uint32_t deviceId;
  uint32_t hClientShare;
  uint32_t hTargetClient;
  uint32_t hTargetDevice;
  uint32_t flags;
  uint64_t vaSpaceSize __attribute__((aligned(8)));
  uint64_t vaStartInternal __attribute__((aligned(8)));
  uint64_t vaLimitInternal __attribute__((aligned(8)));
  uint32_t vaMode;
} __attribute__((packed, aligned(8))) NV0080_ALLOC_PARAMETERS;

typedef struct {
  uint32_t subDeviceId;
} __attribute__((packed, aligned(8))) NV2080_ALLOC_PARAMETERS;

#define NV_VASPACE_ALLOCATION_INDEX_GPU_NEW 0x00U
#define NV_VASPACE_ALLOCATION_FLAGS_IS_EXTERNALLY_OWNED (1U << 3)
typedef struct {
  uint32_t index;
  uint32_t flags;
  uint64_t vaSize __attribute__((aligned(8)));
  uint64_t vaStartInternal __attribute__((aligned(8)));
  uint64_t vaLimitInternal __attribute__((aligned(8)));
  uint32_t bigPageSize;
  uint64_t vaBase __attribute__((aligned(8)));
} __attribute__((packed, aligned(8))) NV_VASPACE_ALLOCATION_PARAMETERS;

#define NV0080_CTRL_CMD_DMA_SET_PAGE_DIRECTORY  0x00801813U
#define NV0080_CTRL_DMA_SET_PAGE_DIRECTORY_FLAGS_APERTURE_SYSMEM_COH 0x00000001U
typedef struct {
  uint64_t physAddress __attribute__((aligned(8)));
  uint32_t numEntries;
  uint32_t flags;
  uint32_t hVASpace;
  uint32_t chId;
  uint32_t subDeviceId;
  uint32_t pasid;
} __attribute__((packed, aligned(8))) NV0080_CTRL_DMA_SET_PAGE_DIRECTORY_PARAMS;

#define NV0080_CTRL_CMD_DMA_UNSET_PAGE_DIRECTORY 0x00801814U
typedef struct {
  uint32_t hVASpace;
  uint32_t subDeviceId;
} __attribute__((packed, aligned(8))) NV0080_CTRL_DMA_UNSET_PAGE_DIRECTORY_PARAMS;

#define NV2080_CTRL_CMD_FIFO_GET_DEVICE_INFO_TABLE 0x20801112U
#define NV2080_CTRL_FIFO_GET_DEVICE_INFO_TABLE_MAX_ENTRIES 32U
#define ENGINE_INFO_TYPE_ENG_DESC 0
#define ENGINE_INFO_TYPE_RM_ENGINE_TYPE 2
#define ENGINE_INFO_TYPE_RUNLIST 3

typedef struct {
  uint32_t engineData[16];
  uint32_t pbdmaIds[2];
  uint32_t pbdmaFaultIds[2];
  uint32_t numPbdmas;
  char engineName[16];
} NV2080_CTRL_FIFO_DEVICE_ENTRY;

typedef struct {
  uint32_t baseIndex;
  uint32_t numEntries;
  uint8_t  bMore;
  uint8_t  pad[3];
  NV2080_CTRL_FIFO_DEVICE_ENTRY entries[32];
} NV2080_CTRL_FIFO_GET_DEVICE_INFO_TABLE_PARAMS;

typedef struct {
  uint16_t engineIdx;
  uint32_t pmcIntrMask;
  uint32_t vectorStall;
  uint32_t vectorNonStall;
} NV2080_CTRL_INTERNAL_INTR_GET_KERNEL_TABLE_ENTRY;

typedef struct {
  uint8_t subtreeStart;
  uint8_t subtreeEnd;
} NV2080_INTR_CATEGORY_SUBTREE_MAP;

typedef struct {
  uint32_t tableLen;
  NV2080_CTRL_INTERNAL_INTR_GET_KERNEL_TABLE_ENTRY table[NV2080_CTRL_INTERNAL_INTR_MAX_TABLE_SIZE];
  NV2080_INTR_CATEGORY_SUBTREE_MAP subtreeMap[NV2080_INTR_CATEGORY_ENUM_COUNT];
} NV2080_CTRL_INTERNAL_INTR_GET_KERNEL_TABLE_PARAMS;

typedef uint8_t NV2080_CTRL_CMD_FB_GET_FB_REGION_SURFACE_MEM_TYPE_FLAG[NV2080_CTRL_CMD_FB_GET_FB_REGION_INFO_MEM_TYPES];

typedef enum {
  COMPUTE_BRANDING_TYPE_NONE = 0,
  COMPUTE_BRANDING_TYPE_TESLA = 1,
} COMPUTE_BRANDING_TYPE;

typedef struct {
  uint64_t base;
  uint64_t limit;
  uint64_t reserved;
  uint32_t performance;
  uint8_t supportCompressed;
  uint8_t supportISO;
  uint8_t bProtected;
  NV2080_CTRL_CMD_FB_GET_FB_REGION_SURFACE_MEM_TYPE_FLAG blackList;
} __attribute__((packed, aligned(8))) NV2080_CTRL_CMD_FB_GET_FB_REGION_FB_REGION_INFO;


typedef struct {
  uint32_t numFBRegions;
  uint8_t pad[4];
  NV2080_CTRL_CMD_FB_GET_FB_REGION_FB_REGION_INFO fbRegion[NV2080_CTRL_CMD_FB_GET_FB_REGION_INFO_MAX_ENTRIES];
} __attribute__((packed, aligned(8))) NV2080_CTRL_CMD_FB_GET_FB_REGION_INFO_PARAMS;

typedef struct {
  uint32_t index;
  uint32_t flags;
  uint32_t length;
  uint8_t data[NV2080_GPU_MAX_GID_LENGTH];
} __attribute__((packed)) NV2080_CTRL_GPU_GET_GID_INFO_PARAMS;

typedef struct {
  uint32_t gpcMask;
} __attribute__((packed)) NV2080_CTRL_GPU_GET_FERMI_GPC_INFO_PARAMS;

typedef struct {
  uint32_t gpcId;
  uint32_t tpcMask;
} __attribute__((packed)) NV2080_CTRL_GPU_GET_FERMI_TPC_INFO_PARAMS;

typedef struct {
  uint32_t gpcId;
  uint32_t zcullMask;
} __attribute__((packed)) NV2080_CTRL_GPU_GET_FERMI_ZCULL_INFO_PARAMS;

typedef struct {
  uint32_t BoardID;
  char chipSKU[4];
  char chipSKUMod[2];
  char project[5];
  char projectSKU[5];
  char CDP[6];
  char projectSKUMod[2];
  uint32_t businessCycle;
} __attribute__((packed)) NV2080_CTRL_BIOS_GET_SKU_INFO_PARAMS;

typedef struct {
  uint32_t totalVFs;
  uint32_t firstVfOffset;
  uint32_t vfFeatureMask;
  uint8_t  pad0[4];
  uint64_t FirstVFBar0Address;
  uint64_t FirstVFBar1Address;
  uint64_t FirstVFBar2Address;
  uint64_t bar0Size;
  uint64_t bar1Size;
  uint64_t bar2Size;
  uint8_t b64bitBar0;
  uint8_t b64bitBar1;
  uint8_t b64bitBar2;
  uint8_t bSriovEnabled;
  uint8_t bSriovHeavyEnabled;
  uint8_t bEmulateVFBar0TlbInvalidationRegister;
  uint8_t bClientRmAllocatedCtxBuffer;
  uint8_t pad1[1];
} __attribute__((packed, aligned(8))) NV0080_CTRL_GPU_GET_SRIOV_CAPS_PARAMS;

typedef struct {
  uint32_t version;
  uint32_t regBankCount;
  uint32_t regBankRegCount;
  uint32_t maxWarpsPerSM;
  uint32_t maxThreadsPerWarp;
  uint32_t geomGsObufEntries;
  uint32_t geomXbufEntries;
  uint32_t maxSPPerSM;
  uint32_t rtCoreCount;
} __attribute__((packed)) GspSMInfo;

typedef struct {
  uint32_t numHeads;
  uint32_t maxNumHeads;
} __attribute__((packed)) VIRTUAL_DISPLAY_GET_NUM_HEADS_PARAMS;

typedef struct {
  uint32_t headIndex;
  uint32_t maxHResolution;
  uint32_t maxVResolution;
} __attribute__((packed)) VIRTUAL_DISPLAY_GET_MAX_RESOLUTION_PARAMS;

typedef struct GspStaticConfigInfo_t {
  uint8_t grCapsBits[NV0080_CTRL_GR_CAPS_TBL_SIZE];
  uint8_t pad0[1];
  NV2080_CTRL_GPU_GET_GID_INFO_PARAMS gidInfo;
  NV2080_CTRL_GPU_GET_FERMI_GPC_INFO_PARAMS gpcInfo;
  NV2080_CTRL_GPU_GET_FERMI_TPC_INFO_PARAMS tpcInfo[MAX_GPC_COUNT];
  NV2080_CTRL_GPU_GET_FERMI_ZCULL_INFO_PARAMS zcullInfo[MAX_GPC_COUNT];
  NV2080_CTRL_BIOS_GET_SKU_INFO_PARAMS SKUInfo;
  NV2080_CTRL_CMD_FB_GET_FB_REGION_INFO_PARAMS fbRegionInfoParams;
  uint32_t computeBranding;
  uint8_t pad1[4];

  NV0080_CTRL_GPU_GET_SRIOV_CAPS_PARAMS sriovCaps;
  uint32_t sriovMaxGfid;

  uint32_t engineCaps[NVGPU_ENGINE_CAPS_MASK_ARRAY_MAX];

  GspSMInfo SM_info;

  uint8_t poisonFuseEnabled;
  uint8_t pad2[7];

  uint64_t fb_length;
  uint32_t fbio_mask;
  uint32_t fb_bus_width;
  uint32_t fb_ram_type;
  uint32_t fbp_mask;
  uint32_t l2_cache_size;

  uint32_t gfxpBufferSize[NV2080_CTRL_CMD_GR_CTXSW_PREEMPTION_BIND_BUFFERS_CONTEXT_POOL];
  uint32_t gfxpBufferAlignment[NV2080_CTRL_CMD_GR_CTXSW_PREEMPTION_BIND_BUFFERS_CONTEXT_POOL];

  uint8_t gpuNameString[NV2080_GPU_MAX_NAME_STRING_LENGTH];
  uint8_t gpuShortNameString[NV2080_GPU_MAX_NAME_STRING_LENGTH];
  uint16_t gpuNameString_Unicode[NV2080_GPU_MAX_NAME_STRING_LENGTH];
  uint8_t bGpuInternalSku;
  uint8_t bIsQuadroGeneric;
  uint8_t bIsQuadroAd;
  uint8_t bIsNvidiaNvs;
  uint8_t bIsVgx;
  uint8_t bGeforceSmb;
  uint8_t bIsTitan;
  uint8_t bIsTesla;
  uint8_t bIsMobile;
  uint8_t bIsGc6Rtd3Allowed;
  uint8_t bIsGcOffRtd3Allowed;
  uint8_t bIsGcoffLegacyAllowed;
  uint8_t pad3[4];

  uint64_t bar1PdeBase;
  uint64_t bar2PdeBase;

  uint8_t bVbiosValid;
  uint8_t pad4[3];
  uint32_t vbiosSubVendor;
  uint32_t vbiosSubDevice;

  uint8_t bPageRetirementSupported;
  uint8_t bSplitVasBetweenServerClientRm;
  uint8_t bClRootportNeedsNosnoopWAR;
  uint8_t pad5[1];

  VIRTUAL_DISPLAY_GET_NUM_HEADS_PARAMS displaylessMaxHeads;
  VIRTUAL_DISPLAY_GET_MAX_RESOLUTION_PARAMS displaylessMaxResolution;
  uint64_t displaylessMaxPixels;

  uint32_t hInternalClient;
  uint32_t hInternalDevice;
  uint32_t hInternalSubdevice;

  uint8_t bSelfHostedMode;
  uint8_t bAtsSupported;
  uint8_t bIsGpuUefi;
  uint8_t pad6[5];
} __attribute__((packed, aligned(8))) GspStaticConfigInfo;


typedef int (*NvGspMsgNtfyFunc)(void *priv, uint32_t fn, const void *repv, uint32_t repc);

typedef struct {
  uint32_t fn;
  NvGspMsgNtfyFunc func;
  void* priv;
} NvGspMsgNtfy;

void nv_gsp_rpc_subsystem_init(NvGspContext *gsp);
int nv_gsp_msg_ntfy_add(NvGspContext *gsp, uint32_t fn, NvGspMsgNtfyFunc func, void *priv);
void *nv_gsp_rpc_get(const NvGspContext *gsp, uint32_t fn, uint32_t payload_size);
void *nv_gsp_rpc_push(const NvDevice *dev, NvGspContext *gsp, void *payload, NvGspRpcReplyPolicy policy, uint32_t expected_reply_len);
void nv_gsp_rpc_done(const NvGspContext *gsp, void *repv);
int nv_gsp_rpc_poll(const NvDevice *dev, NvGspContext *gsp, uint32_t fn);
int nv_rpc_status_to_errno(uint32_t rpc_status);
int nv_gsp_get_static_info(const NvDevice *dev, NvGspContext *gsp, GspStaticConfigInfo *out_info);
void nv_gsp_dump_static_info(const GspStaticConfigInfo *info);
int nv_gsp_intr_get_table(const NvDevice *dev, NvGspContext *gsp, const GspStaticConfigInfo *static_info);

int nv_rm_alloc(const NvDevice *dev, NvGspContext *gsp, uint32_t hClient, uint32_t hParent, uint32_t hObject, uint32_t hClass, void *params, uint32_t params_size);
int nv_rm_control(const NvDevice *dev, NvGspContext *gsp, uint32_t hClient, uint32_t hObject, uint32_t cmd, void *params, uint32_t params_size);
int nv_rm_free(const NvDevice *dev, NvGspContext *gsp, uint32_t hClient, uint32_t hParent, uint32_t hObject);
