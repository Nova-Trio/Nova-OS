#include <virtioGpuProtocol.h>
#include <virtioGpu.h>
#include <caps.h>

int virtioGpuContextCreate(VirtioGpuDevice *gpu, const char *name, uint32_t *outCtxId) {
  if (!gpu || !outCtxId) return -1;

  uint64_t rflags = spin_lock_irqsave(&gpu->ctxLock);
  uint32_t ctxId = gpu->nextContextId++;
  if (ctxId == 0) ctxId = gpu->nextContextId++;
  spin_unlock_irqrestore(&gpu->ctxLock, rflags);

  VirtioGpuCtxCreate req;
  memset(&req, 0, sizeof(req));
  req.hdr.type = VIRTIO_GPU_CMD_CTX_CREATE;
  req.hdr.ctxId = ctxId;

  size_t nameLen = 0;
  if (name) {
    while (name[nameLen] && nameLen < sizeof(req.debugName) - 1) {
      req.debugName[nameLen] = name[nameLen];
      nameLen++;
    }
  }
  req.debugName[nameLen] = '\0';
  req.nlen = (uint32_t)nameLen;

  VirtioGpuCtrlHdr resp;
  memset(&resp, 0, sizeof(resp));

  if (virtioGpuSendControlCmd(gpu, &req, sizeof(req), &resp, sizeof(resp)) != 0) {
    return -1;
  }

  if (resp.type != VIRTIO_GPU_RESP_OK_NODATA) {
    kprintf("[VIRTIO-GPU] CTX_CREATE failed with error 0x%04x\n", resp.type);
    return -1;
  }

  VirtioGpuContext *ctx = (VirtioGpuContext *)kzalloc(sizeof(VirtioGpuContext));
  if (!ctx) {
    VirtioGpuCtxDestroy dreq;
    memset(&dreq, 0, sizeof(dreq));
    dreq.hdr.type = VIRTIO_GPU_CMD_CTX_DESTROY;
    dreq.hdr.ctxId = ctxId;
    virtioGpuSendControlCmd(gpu, &dreq, sizeof(dreq), &resp, sizeof(resp));
    return -1;
  }

  ctx->contextId = ctxId;
  if (name) {
    size_t i = 0;
    while (name[i] && i < sizeof(ctx->name) - 1) {
      ctx->name[i] = name[i];
      i++;
    }
    ctx->name[i] = '\0';
  }

  rflags = spin_lock_irqsave(&gpu->ctxLock);
  ctx->next = gpu->contexts;
  gpu->contexts = ctx;
  spin_unlock_irqrestore(&gpu->ctxLock, rflags);

  *outCtxId = ctxId;
  return 0;
}

int virtioGpuContextDestroy(VirtioGpuDevice *gpu, uint32_t ctxId) {
  if (!gpu || ctxId == 0) return -1;

  uint64_t rflags = spin_lock_irqsave(&gpu->ctxLock);
  VirtioGpuContext **curr = &gpu->contexts;
  VirtioGpuContext *target = NULL;

  while (*curr) {
    if ((*curr)->contextId == ctxId) {
      target = *curr;
      *curr = target->next;
      break;
    }
    curr = &(*curr)->next;
  }
  spin_unlock_irqrestore(&gpu->ctxLock, rflags);

  if (!target) {
    return -1;
  }

  VirtioGpuCtxDestroy req;
  memset(&req, 0, sizeof(req));
  req.hdr.type = VIRTIO_GPU_CMD_CTX_DESTROY;
  req.hdr.ctxId = ctxId;

  VirtioGpuCtrlHdr resp;
  memset(&resp, 0, sizeof(resp));

  virtioGpuSendControlCmd(gpu, &req, sizeof(req), &resp, sizeof(resp));

  kfree(target);
  return (resp.type == VIRTIO_GPU_RESP_OK_NODATA) ? 0 : -1;
}
