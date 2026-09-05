#include "nag.h"
#include <heap.h>
#include <spinlock.h>
#include <console.h>
#include <stddef.h>
#include <stdint.h>

static Spinlock nagLock = SPINLOCK_INIT;

static struct GpuAdapter** adapters = NULL;
static size_t adapterCapacity = 0;
static size_t adapterCount = 0;
static uint32_t nextAdapterId = 0;

void nagInit(void){
  adapters = NULL;
  adapterCapacity = 0;
  adapterCount = 0;
  nextAdapterId = 0;
}

int nagRegisterAdapter(struct GpuAdapter* adapter){
  if(!adapter) return -1;

  uint64_t rflags = spin_lock_irqsave(&nagLock);

  int targetSlot = -1;
  for(size_t i = 0; i < adapterCapacity; i++){
    if(adapters[i] == NULL){
      targetSlot = (int)i;
      break;
    }
  }

  if(targetSlot == -1){
    size_t newCap = (adapterCapacity == 0) ? 4 : adapterCapacity * 2;
    struct GpuAdapter** newTable = (struct GpuAdapter**)krealloc(adapters,newCap*sizeof(struct GpuAdapter*));

    if(!newTable) {
      spin_unlock_irqrestore(&nagLock, rflags);
      kprintf("[NAG] Error: Failed to expand adapter registry\n");
      return -1;
    }

    for (size_t i = adapterCapacity; i < newCap; i++) {
      newTable[i] = NULL;
    }

    targetSlot = (int)adapterCapacity;
    adapters = newTable;
    adapterCapacity = newCap;
  }

  adapter->adapterId = (uint32_t)targetSlot;
  adapters[targetSlot] = adapter;
  adapterCount++;

  spin_unlock_irqrestore(&nagLock, rflags);

  kprintf("[NAG] Registered GPU Adapter %u: \"%s\" (%04x:%04x)\n", adapter->adapterId, adapter->name, adapter->pciVendor, adapter->pciDevice);
  return 0;
}


void nagUnregisterAdapter(struct GpuAdapter *adapter) {
  if (!adapter) return;

  uint64_t rflags = spin_lock_irqsave(&nagLock);

  if (adapter->adapterId < adapterCapacity && adapters[adapter->adapterId] == adapter) {
    adapters[adapter->adapterId] = NULL;
    adapterCount--;
    kprintf("[NAG] Unregistered GPU Adapter %u\n", adapter->adapterId);
  }

  if (adapterCount == 0 && adapters) {
    kfree(adapters);
    adapters = NULL;
    adapterCapacity = 0;
  }

  spin_unlock_irqrestore(&nagLock, rflags);
}

struct GpuAdapter *nagGetAdapter(uint32_t adapter_id) {
  uint64_t rflags = spin_lock_irqsave(&nagLock);

  if (adapter_id >= adapterCapacity) {
    spin_unlock_irqrestore(&nagLock, rflags);
    return NULL;
  }

  struct GpuAdapter *adapter = adapters[adapter_id];
  spin_unlock_irqrestore(&nagLock, rflags);
  return adapter;
}
