#include <stdint.h>

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

  void* priv;
};

int nagRegisterAdapter(struct GpuAdapter* adapter);
void nagUnregisterAdapter(struct GpuAdapter *adapter);
struct GpuAdapter *nagGetAdapter(uint32_t adapter_id);
