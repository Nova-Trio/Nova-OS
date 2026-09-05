#pragma once
#include <stdint.h>
#include <stddef.h>

#define VIRTQ_DESC_F_NEXT 1
#define VIRTQ_DESC_F_WRITE 2
#define VIRTQ_DESC_F_INDIRECT 4

typedef struct {
  uint64_t addr;
  uint32_t len;
  uint16_t flags;
  uint16_t next;
} __attribute__((packed)) VirtqDesc;

typedef struct {
  uint16_t flags;
  uint16_t idx;
  uint16_t ring[];
} __attribute__((packed)) VirtqAvail;

typedef struct {
  uint32_t id;
  uint32_t len;
} __attribute__((packed)) VirtqUsedElem;

typedef struct {
  uint16_t flags;
  uint16_t idx;
  VirtqUsedElem ring[];
} __attribute__((packed)) VirtqUsed;

typedef struct Virtqueue {
  uint16_t queueIndex;
  uint16_t queueSize;
  uint16_t numFree;
  uint16_t freeHead;
  uint16_t lastUsedIdx;

  void *physBase;
  size_t pageCount;

  VirtqDesc *descTable;
  VirtqAvail *availRing;
  VirtqUsed *usedRing;

  volatile uint16_t *notifyAddr;
} Virtqueue;

struct VirtioGpuDevice;

int virtqueueCreate(struct VirtioGpuDevice *gpu, uint16_t queueIndex, Virtqueue **outQueue);
void virtqueueDestroy(Virtqueue *vq);
int virtqueueSubmit(Virtqueue *vq, uint64_t reqPhys, uint32_t reqLen, uint64_t respPhys, uint32_t respLen);
void virtqueueKick(Virtqueue *vq);
int virtqueuePoll(Virtqueue *vq, uint64_t timeoutMs);
