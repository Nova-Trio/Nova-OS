#pragma once
#include <stdint.h>
#include <stddef.h>

// Nova accelerated graphics model

typedef enum {
  NAG_GPU_ENGINE_TYPE_3D = 0, // 3D & compute
  NAG_GPU_ENGINE_TYPE_CE = 1, // copy engine 
  NAG_GPU_ENGINE_TYPE_EN = 2, // video encode
  NAG_GPU_ENGINE_TYPE_DC = 3, // video decode
  NAG_GPU_ENGINE_TYPE_UN = 4, // unspecified
  NAG_GPU_ENGINE_TYPE_IV = 0xFFFF // invalid
} EngineType;


/* Driver should preferably support stuff which advertises in here */
typedef enum {
  NAG_CAP_SUPPORTS_3D = (1ULL << 0), // supports hardware 3D acceleration
  NAG_CAP_SUPPORTS_2D = (1ULL << 1), // supports hardware 2D acceleration
  NAG_CAP_SUPPORTS_HWVID = (1ULL << 2), // supports video encode & decode
  NAG_CAP_SUPPORTS_OFA = (1ULL << 3), // supports optical flow
  NAG_CAP_DISP_MODESET = (1ULL << 4), // supports modesetting
  NAG_CAP_DISP_HWCURSR = (1ULL << 5), // supports hardware cusror
  NAG_CAP_GPGPU = (1ULL << 6), // GeneralPurposeGPU support
  NAG_CAP_SUPPORTS_RT = (1ULL << 7), // advertises GAPI specific ray tracing extensions via any means
  NAG_CAP_SUPPORTS_COMP = (1ULL << 8), // compute support
} GpuCaps;

struct GpuEngine {
  uint32_t engineId;
  EngineType type;
  char name[24];
  void* engine;
};

struct GpuMem {
  uint64_t vramTotal;
  uint64_t vramFree;
  uint64_t shmTotal; // shared memory
  uint64_t shmFree;
};

struct DispMode {
  uint32_t width;
  uint32_t height;
  uint32_t refreshRate;
  uint32_t pitch;
  uint32_t bpp;
};

struct DispHead {
  uint32_t headId;
  uint32_t connected;
  struct DispMode currentMode;

  uint32_t modeCount;
  struct DispMode* modes;
};

struct DispEngine {
  uint32_t headCount;
  struct DispHead* heads;
  void* disp;
};

// if a custom GAPI usermode driver is missing dont advertise support
typedef struct GraphicsAPI {
  char name[16];
  uint32_t version; // XML version, user or another wrapper unpacks it into appropriate maj.min.pat.
} GraphicsAPI;

typedef struct NagGpuProps {
  uint32_t graphicsApisSupported;
  GraphicsAPI* graphicsApis;
  
  uint8_t uuid[16]; // multi gpu uuid identification
  char architecture[32]; // short architecture name with sillicon stepping
  uint32_t bus;
  uint32_t dev;
  uint32_t fun;

  // 0 if not present / unsupported
  uint32_t computeUnits;
  uint32_t shaderCores;
  uint32_t rtCores;
  uint32_t tensorCores;


  char vramType[16]; // short VRAM name eg "GDDR6"
  uint32_t memoryBusWidth;
  uint32_t memoryClockMhz;
  
  uint32_t baseClockMhz;
  uint32_t boostClockMhz; // if boost isnt supported, boost = base
  uint32_t currentClockMhz;
  uint32_t tdpWatts;
  uint32_t currentPowerWatts;
  uint32_t temperatureC;

  // on emulated cards, preferably set 8K max
  uint32_t maxDisplayWidth;
  uint32_t maxDisplayHeight;
  
  char driverVersion[32]; // maybe optional?
  uint32_t driverBuildDate; // optional
} NagGpuProps;

typedef struct NagContextCreateArgs {
  char name[32]; // usually can be used for debug tracking
  uint32_t contextId; // engine id, be creative with this
} NagContextCreateArgs;

typedef struct NagContextDestroyArgs {
  uint32_t contextId;
} NagContextDestroyArgs;


typedef enum {
  NAG_RES_TYPE_BUFFER = 0,
  NAG_RES_TYPE_2D = 1,
  NAG_RES_TYPE_OTHER = 2,

  NAG_RES_TYPE_INVAL = 0xffff,
} NagResourceType;

typedef enum {
  NAG_RES_USAGE_RENDER_TARGET = (1U << 0),
  NAG_RES_USAGE_VERTEX_BUFFER = (1U << 1),
  NAG_RES_USAGE_INDEX_BUFFER = (1U << 2),
  NAG_RES_USAGE_TEX = (1U << 3),
  NAG_RES_USAGE_CONST = (1U << 4),
  NAG_RES_USAGE_STAGING = (1U << 5),
} NagResourceUsage;

typedef enum {
  NAG_FORMAT_NONE = 0,
  NAG_FORMAT_B8G8R8A8_UNORM = 1,
  NAG_FORMAT_R8G8B8A8_UNORM = 2,
  NAG_FORMAT_R32G32B32A32_FLOAT = 3,

} NagPixelFormat;

typedef struct NagResourceCreateArgs {
  uint32_t contextId;
  NagResourceType type;
  uint32_t usage;
  NagPixelFormat format;
  uint32_t width;
  uint32_t height;
  uint32_t depth;

  uint32_t resourceId;
  uint64_t cpuAddress;
  uint64_t size;
} NagResourceCreateArgs;

typedef struct NagResourceDestroyArgs {
  uint32_t contextId;
  uint32_t resourceId;
} NagResourceDestroyArgs;

// your display driver must accept all these operations and handle them according to the support
typedef enum {
  NAG_GPU_OP_QUERY = 0,
  NAG_GPU_OP_CONTEXT_CREATE = 1,
  NAG_GPU_OP_CONTEXT_DESTROY = 2,
  NAG_GPU_OP_RESOURCE_CREATE = 3,
  NAG_GPU_OP_RESOURCE_DESTROY = 4,
  NAG_GPU_OP_SUBMIT = 5,
  NAG_GPU_OP_WAIT_FENCE = 6,


  NAG_GPU_OP_INVAL = 0xffff,
} NagOp;

struct GpuAdapter {
  uint32_t adapterId;
  char name[64]; // preferably a long name instead of shortened
  uint32_t pciVendor;
  uint32_t pciDevice;

  uint64_t caps;

  struct GpuMem mem;
  struct DispEngine disp;

  uint32_t engineCount;
  struct GpuEngine* engines;

  int (*dispatchOp)(struct GpuAdapter *adapter, NagOp op, void *arg, size_t argSize);

  void* priv;
};



int nagRegisterAdapter(struct GpuAdapter* adapter);
void nagUnregisterAdapter(struct GpuAdapter *adapter);
struct GpuAdapter *nagGetAdapter(uint32_t adapter_id);
int nagDispatch(uint32_t adapterId, NagOp op, void *arg, size_t argSize);
