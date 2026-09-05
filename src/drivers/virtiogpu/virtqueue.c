#include "virtqueue.h"
#include "virtioGpu.h"

int virtqueueCreate(VirtioGpuDevice *gpu, uint16_t queueIndex, Virtqueue **outQueue) {
  VirtioPciCommonCfg *cfg = gpu->commonCfg;

  cfg->queueSelect = queueIndex;
  __asm__ volatile("mfence" ::: "memory");

  uint16_t maxQueueSize = cfg->queueSize;
  if (maxQueueSize == 0) {
    kprintf("[VIRTIO-GPU] Queue %u not supported by hardware\n", (uint32_t)queueIndex);
    return -1;
  }

  uint16_t queueSize = maxQueueSize;

  size_t descBytes = sizeof(VirtqDesc) * queueSize;
  size_t availBytes = sizeof(VirtqAvail) + (sizeof(uint16_t) * queueSize) + sizeof(uint16_t);
  size_t usedBytes = sizeof(VirtqUsed) + (sizeof(VirtqUsedElem) * queueSize) + sizeof(uint16_t);

  size_t descOffset = 0;
  size_t availOffset = (descOffset + descBytes + 1) & ~1ULL;
  size_t usedOffset = (availOffset + availBytes + 63) & ~63ULL;
  size_t totalBytes = usedOffset + usedBytes;

  size_t pageCount = (totalBytes + PAGE_SIZE - 1) / PAGE_SIZE;
  void *physBase = pmm_alloc_frames(pageCount);
  if (!physBase) {
    kprintf("[VIRTIO-GPU] Failed to allocate physical memory for Queue %u\n", (uint32_t)queueIndex);
    return -1;
  }

  uint8_t *virtBase = (uint8_t *)((uint64_t)physBase + HHDM_BASE);
  memset(virtBase, 0, pageCount * PAGE_SIZE);

  Virtqueue *vq = (Virtqueue *)kzalloc(sizeof(Virtqueue));
  if (!vq) {
    pmm_free_frames(physBase, pageCount);
    return -1;
  }

  vq->queueIndex = queueIndex;
  vq->queueSize = queueSize;
  vq->numFree = queueSize;
  vq->freeHead = 0;
  vq->lastUsedIdx = 0;
  vq->physBase = physBase;
  vq->pageCount = pageCount;

  vq->descTable = (VirtqDesc *)(virtBase + descOffset);
  vq->availRing = (VirtqAvail *)(virtBase + availOffset);
  vq->usedRing = (VirtqUsed *)(virtBase + usedOffset);

  for (uint16_t i = 0; i < queueSize - 1; i++) {
    vq->descTable[i].next = i + 1;
  }
  vq->descTable[queueSize - 1].next = 0xFFFF;

  uint16_t notifyOffset = cfg->queueNotifyOff;
  uintptr_t notifyAddr = (uintptr_t)gpu->notifyBase + (notifyOffset * gpu->notifyOffMultiplier);
  vq->notifyAddr = (volatile uint16_t *)notifyAddr;

  uint64_t descPhys = (uint64_t)physBase + descOffset;
  uint64_t availPhys = (uint64_t)physBase + availOffset;
  uint64_t usedPhys = (uint64_t)physBase + usedOffset;

  cfg->queueDesc = descPhys;
  __asm__ volatile("mfence" ::: "memory");
  cfg->queueDriver = availPhys;
  __asm__ volatile("mfence" ::: "memory");
  cfg->queueDevice = usedPhys;
  __asm__ volatile("mfence" ::: "memory");
  cfg->queueSize = queueSize;
  __asm__ volatile("mfence" ::: "memory");
  cfg->queueEnable = 1;
  __asm__ volatile("mfence" ::: "memory");

  *outQueue = vq;
  return 0;
}

void virtqueueDestroy(Virtqueue *vq) {
  if (!vq) return;

  if (vq->physBase && vq->pageCount > 0) {
    pmm_free_frames(vq->physBase, vq->pageCount);
  }

  kfree(vq);
}

int virtqueueSubmit(Virtqueue *vq, uint64_t reqPhys, uint32_t reqLen, uint64_t respPhys, uint32_t respLen) {
  if (vq->numFree < 2) {
    return -1;
  }

  uint16_t headIdx = vq->freeHead;
  uint16_t respIdx = vq->descTable[headIdx].next;

  vq->freeHead = vq->descTable[respIdx].next;
  vq->numFree -= 2;

  vq->descTable[headIdx].addr = reqPhys;
  vq->descTable[headIdx].len = reqLen;
  vq->descTable[headIdx].flags = VIRTQ_DESC_F_NEXT;
  vq->descTable[headIdx].next = respIdx;

  vq->descTable[respIdx].addr = respPhys;
  vq->descTable[respIdx].len = respLen;
  vq->descTable[respIdx].flags = VIRTQ_DESC_F_WRITE;
  vq->descTable[respIdx].next = 0xFFFF;

  uint16_t availIdx = vq->availRing->idx;
  vq->availRing->ring[availIdx % vq->queueSize] = headIdx;

  __asm__ volatile("mfence" ::: "memory");
  vq->availRing->idx = availIdx + 1;
  __asm__ volatile("mfence" ::: "memory");

  return 0;
}

void virtqueueKick(Virtqueue *vq) {
  __asm__ volatile("mfence" ::: "memory");
  *vq->notifyAddr = vq->queueIndex;
  __asm__ volatile("mfence" ::: "memory");
}

int virtqueuePoll(Virtqueue *vq, uint64_t timeoutMs) {
  uint64_t start = hpet_get_millis();

  while (vq->lastUsedIdx == vq->usedRing->idx) {
    if ((hpet_get_millis() - start) >= timeoutMs) {
      kprintf("[VIRTIO-GPU] Timeout waiting for Queue %u response\n", (uint32_t)vq->queueIndex);
      return -1;
    }
    __asm__ volatile("pause");
  }

  __asm__ volatile("mfence" ::: "memory");

  while (vq->lastUsedIdx != vq->usedRing->idx) {
    uint16_t usedSlot = vq->lastUsedIdx % vq->queueSize;
    uint32_t headDesc = vq->usedRing->ring[usedSlot].id;

    uint16_t curr = (uint16_t)headDesc;
    uint16_t tail = curr;
    uint16_t count = 1;

    while (vq->descTable[tail].flags & VIRTQ_DESC_F_NEXT) {
      tail = vq->descTable[tail].next;
      count++;
    }

    vq->descTable[tail].next = vq->freeHead;
    vq->freeHead = curr;
    vq->numFree += count;

    vq->lastUsedIdx++;
  }

  return 0;
}
