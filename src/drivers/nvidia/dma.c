#include "device.h"
#include <novamod.h>
#include <stddef.h>
#include <stdint.h>

int nvGspMemAlloc(NvDevice *dev, size_t size, NvGspMem *mem){
  (void)dev;
  if(!mem || size == 0) return -1;
  
  size_t alignedSize = (size + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1);
  size_t pages = alignedSize / PAGE_SIZE;

  void* phys = pmm_alloc_aligned_frames(pages, 1);
  if(!phys) return -1;

  mem->size = alignedSize;
  mem->addr = (uint64_t)phys;
  mem->data = (void*)((uint64_t)phys + HHDM_BASE);

  memset(mem->data, 0, alignedSize);
  return 0;
}

void nvGspMemFree(NvDevice *dev, NvGspMem *mem){
  (void)dev;
  if(!mem || !mem->data || mem->size == 0) return;

  memset(mem->data, 0xFF, mem->size);

  size_t pages = mem->size / PAGE_SIZE;
  pmm_free_frames((void*)mem->addr, pages);

  mem->size = 0;
  mem->addr = 0;
  mem->data = NULL;
}
