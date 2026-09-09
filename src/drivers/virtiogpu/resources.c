#include <stdint.h>
#include <virtioGpuProtocol.h>
#include <virtioGpu.h>
#include <caps.h>

#define GPU_USER_MEM_BASE 0x0000600000000000ULL
#define GPU_USER_MEM_MAX  0x0000700000000000ULL

static uint64_t gGpuUserVirtCursor = GPU_USER_MEM_BASE;
static Spinlock gGpuUserVirtLock = SPINLOCK_INIT;

static uint32_t translatePixelFormat(NagPixelFormat format, uint32_t* outBpp){
  switch (format){
    case NAG_FORMAT_B8G8R8A8_UNORM:
      *outBpp = 4;
      return 1;
    case NAG_FORMAT_R8G8B8A8_UNORM:
      *outBpp = 4;
      return 67;
    case NAG_FORMAT_R32G32B32A32_FLOAT:
      *outBpp = 16;
      return 31;
    case NAG_FORMAT_NONE:
    default:
      *outBpp = 1;
      return 0;
  }
}

static uint32_t translateBindFlags(uint32_t usage, NagResourceType type){
  uint32_t bind = 0;
  if(usage & NAG_RES_USAGE_RENDER_TARGET) bind |= (1U << 1);
  if(usage & NAG_RES_USAGE_TEX) bind |= (1U << 3);
  if(usage & NAG_RES_USAGE_VERTEX_BUFFER) bind |= (1U << 4);
  if(usage & NAG_RES_USAGE_INDEX_BUFFER) bind |= (1U << 5);
  if(usage & NAG_RES_USAGE_CONST) bind |= (1U << 6);
  if(usage & NAG_RES_USAGE_STAGING) bind |= (1U << 17);

  if(bind == 0){
    if(type == NAG_RES_TYPE_BUFFER){
      bind = (1U << 4) | (1U << 5);
    }else{
      bind = (1U << 1) | (1U << 3);
    }
  }
  return bind;
}

int virtioGpuResourceCreate(VirtioGpuDevice *gpu, const NagResourceCreateArgs *args, uint32_t *outResId, uint64_t *outCpuAddr, uint64_t *outSize) {
  if (!gpu || !args || !outResId || !outCpuAddr || !outSize) return -1;
  if (args->width == 0) return -1;

  uint32_t bpp = 1;
  uint32_t virglFormat = 0;
  uint32_t virglTarget = 0;
  size_t byteSize = 0;

  if (args->type == NAG_RES_TYPE_BUFFER) {
    virglTarget = 0;
    virglFormat = 0;
    byteSize = args->width;
  } else if (args->type == NAG_RES_TYPE_2D) {
    if (args->height == 0) return -1;
    virglTarget = 2;
    virglFormat = translatePixelFormat(args->format, &bpp);
    uint32_t depth = args->depth ? args->depth : 1;
    byteSize = (size_t)args->width * args->height * depth * bpp;
  } else {
    return -1;
  }

  uint32_t virglBind = translateBindFlags(args->usage, args->type);
  size_t pageCount = (byteSize + PAGE_SIZE - 1) / PAGE_SIZE;

  void *physFrames = pmm_alloc_frames(pageCount);
  if (!physFrames) {
    return -1;
  }

  void *virtAddr = (void *)((uint64_t)physFrames + HHDM_BASE);
  memset(virtAddr, 0, pageCount * PAGE_SIZE);

  uint64_t rflags = spin_lock_irqsave(&gpu->resLock);
  uint32_t resId = gpu->nextResourceId++;
  if (resId == 0) resId = gpu->nextResourceId++;
  spin_unlock_irqrestore(&gpu->resLock, rflags);

  VirtioGpuResourceCreate3d createReq;
  memset(&createReq, 0, sizeof(createReq));
  createReq.hdr.type = VIRTIO_GPU_CMD_RESOURCE_CREATE_3D;
  createReq.hdr.ctxId = args->contextId;
  createReq.resourceId = resId;
  createReq.target = virglTarget;
  createReq.format = virglFormat;
  createReq.bind = virglBind;
  createReq.width = args->width;
  createReq.height = (args->type == NAG_RES_TYPE_BUFFER) ? 1 : args->height;
  createReq.depth = (args->type == NAG_RES_TYPE_BUFFER) ? 1 : (args->depth ? args->depth : 1);
  createReq.arraySize = 1;
  createReq.lastLevel = 0;
  createReq.nrSamples = 0;
  createReq.flags = 0;

  VirtioGpuCtrlHdr resp;
  memset(&resp, 0, sizeof(resp));

  if (virtioGpuSendControlCmd(gpu, &createReq, sizeof(createReq), &resp, sizeof(resp)) != 0 ||
      resp.type != VIRTIO_GPU_RESP_OK_NODATA) {
    pmm_free_frames(physFrames, pageCount);
    return -1;
  }

  struct {
    VirtioGpuResourceAttachBacking req;
    VirtioGpuMemEntry entry;
  } __attribute__((packed)) attachPkt;

  memset(&attachPkt, 0, sizeof(attachPkt));
  attachPkt.req.hdr.type = VIRTIO_GPU_CMD_RESOURCE_ATTACH_BACKING;
  attachPkt.req.resourceId = resId;
  attachPkt.req.nrEntries = 1;
  attachPkt.entry.addr = (uint64_t)physFrames;
  attachPkt.entry.length = (uint32_t)(pageCount * PAGE_SIZE);

  memset(&resp, 0, sizeof(resp));
  if (virtioGpuSendControlCmd(gpu, &attachPkt, sizeof(attachPkt), &resp, sizeof(resp)) != 0 ||
      resp.type != VIRTIO_GPU_RESP_OK_NODATA) {
    VirtioGpuResourceUnref unrefReq;
    memset(&unrefReq, 0, sizeof(unrefReq));
    unrefReq.hdr.type = VIRTIO_GPU_CMD_RESOURCE_UNREF;
    unrefReq.resourceId = resId;
    virtioGpuSendControlCmd(gpu, &unrefReq, sizeof(unrefReq), &resp, sizeof(resp));
    pmm_free_frames(physFrames, pageCount);
    return -1;
  }

  if (args->contextId != 0) {
    VirtioGpuCtxAttachResource ctxAttach;
    memset(&ctxAttach, 0, sizeof(ctxAttach));
    ctxAttach.hdr.type = VIRTIO_GPU_CMD_CTX_ATTACH_RESOURCE;
    ctxAttach.hdr.ctxId = args->contextId;
    ctxAttach.resourceId = resId;

    memset(&resp, 0, sizeof(resp));
    if (virtioGpuSendControlCmd(gpu, &ctxAttach, sizeof(ctxAttach), &resp, sizeof(resp)) != 0 ||
        resp.type != VIRTIO_GPU_RESP_OK_NODATA) {
      VirtioGpuResourceDetachBacking detachBacking;
      memset(&detachBacking, 0, sizeof(detachBacking));
      detachBacking.hdr.type = VIRTIO_GPU_CMD_RESOURCE_DETACH_BACKING;
      detachBacking.resourceId = resId;
      virtioGpuSendControlCmd(gpu, &detachBacking, sizeof(detachBacking), &resp, sizeof(resp));

      VirtioGpuResourceUnref unrefReq;
      memset(&unrefReq, 0, sizeof(unrefReq));
      unrefReq.hdr.type = VIRTIO_GPU_CMD_RESOURCE_UNREF;
      unrefReq.resourceId = resId;
      virtioGpuSendControlCmd(gpu, &unrefReq, sizeof(unrefReq), &resp, sizeof(resp));

      pmm_free_frames(physFrames, pageCount);
      return -1;
    }
  }

  VirtioGpuResource *res = (VirtioGpuResource *)kzalloc(sizeof(VirtioGpuResource));
  if (!res) {
    virtioGpuResourceDestroy(gpu, args->contextId, resId);
    return -1;
  }

  uint64_t userVirt = 0;
  Thread *currThread = schedCurrent();
  Process *currProc = currThread ? currThread->process : NULL;

  if (currProc && currProc->pml4) {
    uint64_t vflags = spin_lock_irqsave(&gGpuUserVirtLock);
    userVirt = gGpuUserVirtCursor;
    gGpuUserVirtCursor += pageCount * PAGE_SIZE;
    if (gGpuUserVirtCursor >= GPU_USER_MEM_MAX) {
      gGpuUserVirtCursor = GPU_USER_MEM_BASE;
    }
    spin_unlock_irqrestore(&gGpuUserVirtLock, vflags);

    vmaCreate(currProc, userVirt, pageCount * PAGE_SIZE, VMA_READ | VMA_WRITE | VMA_USER);
    if (vmm_map_range(currProc->pml4, userVirt, (uint64_t)physFrames, pageCount * PAGE_SIZE,
                      VMM_FLAG_PRESENT | VMM_FLAG_WRITABLE | VMM_FLAG_USER) != 0) {
      vmaDestroy(currProc, userVirt, pageCount * PAGE_SIZE);
      userVirt = 0;
    }
  }

  res->resourceId = resId;
  res->contextId = args->contextId;
  res->width = args->width;
  res->height = (args->type == NAG_RES_TYPE_BUFFER) ? 1 : args->height;
  res->depth = (args->type == NAG_RES_TYPE_BUFFER) ? 1 : (args->depth ? args->depth : 1);
  res->format = virglFormat;
  res->target = virglTarget;
  res->bind = virglBind;
  res->physBase = physFrames;
  res->pageCount = pageCount;
  res->byteSize = byteSize;
  res->userVirt = userVirt;
  res->proc = currProc;

  rflags = spin_lock_irqsave(&gpu->resLock);
  res->next = gpu->resources;
  gpu->resources = res;
  spin_unlock_irqrestore(&gpu->resLock, rflags);

  *outResId = resId;
  *outCpuAddr = userVirt ? userVirt : (uint64_t)virtAddr;
  *outSize = byteSize;
  return 0;
}

int virtioGpuResourceDestroy(VirtioGpuDevice *gpu, uint32_t ctxId, uint32_t resId) {
  if (!gpu || resId == 0) return -1;

  uint64_t rflags = spin_lock_irqsave(&gpu->resLock);
  VirtioGpuResource **curr = &gpu->resources;
  VirtioGpuResource *target = NULL;

  while (*curr) {
    if ((*curr)->resourceId == resId) {
      target = *curr;
      *curr = target->next;
      break;
    }
    curr = &(*curr)->next;
  }
  spin_unlock_irqrestore(&gpu->resLock, rflags);

  if (!target) {
    return -1;
  }

  VirtioGpuCtrlHdr resp;
  memset(&resp, 0, sizeof(resp));

  if (ctxId != 0 || target->contextId != 0) {
    uint32_t targetCtx = ctxId ? ctxId : target->contextId;
    VirtioGpuCtxDetachResource detachReq;
    memset(&detachReq, 0, sizeof(detachReq));
    detachReq.hdr.type = VIRTIO_GPU_CMD_CTX_DETACH_RESOURCE;
    detachReq.hdr.ctxId = targetCtx;
    detachReq.resourceId = resId;
    virtioGpuSendControlCmd(gpu, &detachReq, sizeof(detachReq), &resp, sizeof(resp));
  }

  VirtioGpuResourceDetachBacking detachBacking;
  memset(&detachBacking, 0, sizeof(detachBacking));
  detachBacking.hdr.type = VIRTIO_GPU_CMD_RESOURCE_DETACH_BACKING;
  detachBacking.resourceId = resId;
  virtioGpuSendControlCmd(gpu, &detachBacking, sizeof(detachBacking), &resp, sizeof(resp));

  VirtioGpuResourceUnref unrefReq;
  memset(&unrefReq, 0, sizeof(unrefReq));
  unrefReq.hdr.type = VIRTIO_GPU_CMD_RESOURCE_UNREF;
  unrefReq.resourceId = resId;
  virtioGpuSendControlCmd(gpu, &unrefReq, sizeof(unrefReq), &resp, sizeof(resp));

  if (target->userVirt && target->proc && target->proc->pml4) {
    vmm_unmap_range(target->proc->pml4, target->userVirt, target->pageCount * PAGE_SIZE);
    vmaDestroy(target->proc, target->userVirt, target->pageCount * PAGE_SIZE);
  }

  if (target->physBase && target->pageCount > 0) {
    pmm_free_frames(target->physBase, target->pageCount);
  }

  kfree(target);
  return 0;
}
