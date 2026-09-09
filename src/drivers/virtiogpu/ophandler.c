#include <novamod.h>
#include <stddef.h>
#include <stdint.h>
#include <virtioGpu.h>

// evil stuff
#define __YEAR_INT__ ( (__DATE__[7] - '0') * 1000 + (__DATE__[8] - '0') * 100 + (__DATE__[9] - '0') * 10 + (__DATE__[10] - '0') )
#define __MONTH_INT__ ( __DATE__[0] == 'J' ? (__DATE__[2] == 'n' ? (__DATE__[1] == 'a' ? 1 : 6) : 7) : __DATE__[0] == 'F' ? 2 : __DATE__[0] == 'M' ? (__DATE__[2] == 'r' ? 3 : 5) : \
                        __DATE__[0] == 'A' ? (__DATE__[2] == 'r' ? 4 : 8) : \
                        __DATE__[0] == 'S' ? 9 : \
                        __DATE__[0] == 'O' ? 10 : \
                        __DATE__[0] == 'N' ? 11 : 12 )

#define __DAY_INT__ ( (__DATE__[4] == ' ' ? 0 : (__DATE__[4] - '0')) * 10 + (__DATE__[5] - '0') )

#define BUILD_DATE_INT (__YEAR_INT__ * 10000 + __MONTH_INT__ * 100 + __DAY_INT__)

// less evil stuff

static void CopyString(char* dest, const char* src, size_t len){
  size_t i = 0;
  while(src[i] && (i + 1) < len) {
    dest[i] = src[i];
    i++;
  }
  dest[i] = '\0';
}

static int VirtioGpuQuery(struct GpuAdapter* adapter, void* arg, size_t argSize){
  if(!arg || argSize < sizeof(NagGpuProps)) return -1;

  NagGpuProps* props = (NagGpuProps*)arg;
  VirtioGpuDevice* gpu = (VirtioGpuDevice*)adapter->priv;
  if (!gpu) return -1;

  const PciDevice* pciDev = gpu->pciDev;

  memset(props->uuid, 0, sizeof(props->uuid));


  CopyString(props->architecture, "N/A", sizeof(props->architecture));

  if(pciDev) {
    props->bus = pciDev->bus;
    props->dev = pciDev->device;
    props->fun = pciDev->function;
  } else {
    props->bus = 0;
    props->dev = 0;
    props->fun = 0;
  }

  props->computeUnits = 0;
  props->shaderCores = 0;
  props->rtCores = 0;
  props->tensorCores = 0;

  CopyString(props->vramType, "N/A", sizeof(props->vramType));
  props->memoryBusWidth = 0;
  props->memoryClockMhz = 0;

  props->baseClockMhz = 0;
  props->boostClockMhz = 0;
  props->currentClockMhz = 0;

  props->tdpWatts = 0;
  props->currentPowerWatts = 0;
  props->temperatureC = 0;

  props->maxDisplayWidth = 7680;
  props->maxDisplayHeight = 4320;

  CopyString(props->driverVersion, "0.0.3", sizeof(props->driverVersion));
  props->driverBuildDate = BUILD_DATE_INT;

  GraphicsAPI apis[2];
  uint32_t apiCount = 0;
  if(gpu->hasVenus){
    CopyString(apis[apiCount].name, "Vulkan", sizeof(apis[apiCount].name));
    apis[apiCount].version = gpu->apiver;
    apiCount++;
  }

  if(gpu->hasVirgl){
    CopyString(apis[apiCount].name, "OpenGL", sizeof(apis[apiCount].name));
    apis[apiCount].version = gpu->apiver;
    apiCount++;
  }

  if (props->graphicsApis && props->graphicsApisSupported > 0 && apiCount > 0) {
    uint32_t toCopy = props->graphicsApisSupported < apiCount ? props->graphicsApisSupported : apiCount;
    size_t copyBytes = toCopy * sizeof(GraphicsAPI);
    if (validate_user_range(props->graphicsApis, copyBytes, 1)) {
      copy_to_user(props->graphicsApis, apis, copyBytes);
    }
  }

  props->graphicsApisSupported = apiCount;
  return 0;
}

int virtioGpuDispatch(struct GpuAdapter* adapter, NagOp op, void* arg, size_t argSize){
  if(!adapter) return -1;

  switch (op) {
    case NAG_GPU_OP_QUERY:
      return VirtioGpuQuery(adapter, arg, argSize);

    case NAG_GPU_OP_CONTEXT_CREATE: {
      if (!arg || argSize < sizeof(NagContextCreateArgs)) return -1;
      NagContextCreateArgs *args = (NagContextCreateArgs *)arg;
      VirtioGpuDevice *gpu = (VirtioGpuDevice *)adapter->priv;
      return virtioGpuContextCreate(gpu, args->name, &args->contextId);
    }

    case NAG_GPU_OP_CONTEXT_DESTROY: {
      if (!arg || argSize < sizeof(NagContextDestroyArgs)) return -1;
      NagContextDestroyArgs *args = (NagContextDestroyArgs *)arg;
      VirtioGpuDevice *gpu = (VirtioGpuDevice *)adapter->priv;
      return virtioGpuContextDestroy(gpu, args->contextId);
    }

    default:
      return -1;
  }
}
