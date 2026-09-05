#include <virtioGpuProtocol.h>
#include <virtioGpu.h>

int virtioGpuSendControlCmd(VirtioGpuDevice *gpu, const void *req, uint32_t reqLen, void *resp, uint32_t respLen);
int virtioGpuGetCapset(VirtioGpuDevice *gpu, uint32_t capsetId, uint32_t capsetVersion, uint32_t capsetMaxSize, void *outCapData, size_t outDataSize);
void virtioGpuDetectCapsets(VirtioGpuDevice *gpu);
