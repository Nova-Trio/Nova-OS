#include <nv_device.h>

#define NV_PRAMIN_BAR1_STATUS_BUSY 0x0000000FU

int nvVramInit(NvDevice *dev, uint64_t phys_base, uint64_t size);
void nvVramCleanup(NvDevice *dev);
int nvVramAlloc(NvDevice *dev, size_t size, uint64_t align, uint64_t *out_phys);
void nvVramFree(NvDevice *dev, uint64_t phys_addr, size_t size);
void nvVramDump(const NvDevice *dev);
int nvVramTest(NvDevice *dev);

int nvBar1Init(NvDevice *dev);
void nvBar1Cleanup(NvDevice *dev);
int nvBar1Map(NvDevice *dev, uint64_t phys_addr, size_t size, void **out_cpu_ptr, uint64_t *out_bar1_va);
void nvBar1Unmap(NvDevice *dev, uint64_t bar1_va, size_t size);
