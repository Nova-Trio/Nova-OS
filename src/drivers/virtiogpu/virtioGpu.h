#pragma once
#include <novamod.h>
#include "virtioPci.h"
#include "virtqueue.h"
#include "virtioGpuProtocol.h"
#include <stdint.h>

typedef struct {
  uint64_t physAddr;
  uint64_t virtAddr;
  uint64_t size;
  uint8_t mapped;
} VirtioGpuBar;

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

  uint8_t hasVenus;
  uint8_t hasVirgl;

  struct GpuAdapter adapter;
} VirtioGpuDevice;

int virtioGpuProbe(const PciDevice *pciDev);
void virtioGpuRemove(void);
