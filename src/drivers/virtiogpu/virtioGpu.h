#pragma once
#include <novamod.h>
#include "virtioPci.h"
#include "virtqueue.h"
#include <virtioGpuProtocol.h>
#include <stdint.h>

typedef struct {
  uint64_t physAddr;
  uint64_t virtAddr;
  uint64_t size;
  uint8_t mapped;
} VirtioGpuBar;

typedef struct VirtioGpuContext {
  uint32_t contextId;
  char name[32];
  struct VirtioGpuContext *next;
} VirtioGpuContext;

typedef struct VirtioGpuDevice{
  const PciDevice *pciDev;
  VirtioGpuBar bars[6];

  VirtioPciCommonCfg *commonCfg;
  volatile uint8_t *isrCfg;
  volatile void *notifyBase;
  uint32_t notifyOffMultiplier;
  VirtioGpuConfig *deviceCfg;

  uint64_t hostFeatures;
  uint64_t negotiatedFeatures;

  Virtqueue* controlQueue;
  Virtqueue* cursorQueue;

  void *ctrlDmaPhys;
  void *ctrlDmaVirt;
  Spinlock ctrlLock;

  uint8_t hasVenus;
  uint8_t hasVirgl;
  uint32_t apiver;

  VirtioGpuContext* contexts;
  uint32_t nextContextId;
  Spinlock ctxLock;

  struct GpuAdapter adapter;
} VirtioGpuDevice;

int virtioGpuProbe(const PciDevice *pciDev);
void virtioGpuRemove(void);

int virtioGpuDispatch(struct GpuAdapter *adapter, NagOp op, void *arg, size_t argSize);

int virtioGpuContextCreate(VirtioGpuDevice *gpu, const char *name, uint32_t *outCtxId);
int virtioGpuContextDestroy(VirtioGpuDevice *gpu, uint32_t ctxId);
