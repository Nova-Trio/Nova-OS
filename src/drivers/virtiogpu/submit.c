#include <stdint.h>
#include <virtioGpuProtocol.h>
#include <virtioGpu.h>
#include <caps.h>



int virtioGpuSubmit(VirtioGpuDevice *gpu, const NagSubmitArgs *args, uint64_t *outFence) {
  if (!gpu || !args || !args->commands || args->commandSize == 0 || (args->commandSize % 4) != 0) {
    return -1;
  }

  size_t totalBytes = sizeof(VirtioGpuCmdSubmit3d) + args->commandSize;
  size_t reqPages = (totalBytes + PAGE_SIZE - 1) / PAGE_SIZE;

  void *reqPhys = pmm_alloc_frames(reqPages);
  void *respPhys = pmm_alloc_frame();
  if (!reqPhys || !respPhys) {
    if (reqPhys) pmm_free_frames(reqPhys, reqPages);
    if (respPhys) pmm_free_frame(respPhys);
    return -1;
  }

  uint8_t *reqVirt = (uint8_t *)((uint64_t)reqPhys + HHDM_BASE);
  VirtioGpuCtrlHdr *respVirt = (VirtioGpuCtrlHdr *)((uint64_t)respPhys + HHDM_BASE);
  memset(respVirt, 0, sizeof(VirtioGpuCtrlHdr));

  uint64_t fflags = spin_lock_irqsave(&gpu->fenceLock);
  uint64_t fence = gpu->nextFenceId++;
  spin_unlock_irqrestore(&gpu->fenceLock, fflags);

  VirtioGpuCmdSubmit3d *cmd = (VirtioGpuCmdSubmit3d *)reqVirt;
  memset(cmd, 0, sizeof(VirtioGpuCmdSubmit3d));
  cmd->hdr.type = VIRTIO_GPU_CMD_SUBMIT_3D;
  cmd->hdr.flags = VIRTIO_GPU_FLAG_FENCE;
  cmd->hdr.ctxId = args->contextId;
  cmd->hdr.fenceId = fence;
  cmd->size = (uint32_t)args->commandSize;

  uint8_t *payloadDst = reqVirt + sizeof(VirtioGpuCmdSubmit3d);
  if (validate_user_range(args->commands, args->commandSize, 0)) {
    copy_from_user(payloadDst, args->commands, args->commandSize);
  } else {
    memcpy(payloadDst, args->commands, args->commandSize);
  }

  VirtqBuf bufs[2] = {
    { .physAddr = (uint64_t)reqPhys, .len = (uint32_t)totalBytes, .write = 0 },
    { .physAddr = (uint64_t)respPhys, .len = (uint32_t)sizeof(VirtioGpuCtrlHdr), .write = 1 }
  };

  uint64_t cflags = spin_lock_irqsave(&gpu->ctrlLock);
  int ret = virtqueueSubmitSg(gpu->controlQueue, bufs, 2);
  if (ret == 0) {
    virtqueueKick(gpu->controlQueue);
    ret = virtqueuePoll(gpu->controlQueue, 2000);
  }
  spin_unlock_irqrestore(&gpu->ctrlLock, cflags);

  if (ret == 0 && respVirt->type == VIRTIO_GPU_RESP_OK_NODATA) {
    fflags = spin_lock_irqsave(&gpu->fenceLock);
    if (fence > gpu->lastCompletedFence) {
      gpu->lastCompletedFence = fence;
    }
    spin_unlock_irqrestore(&gpu->fenceLock, fflags);

    if (outFence) {
      *outFence = fence;
    }
  } else {
    ret = -1;
  }

  pmm_free_frames(reqPhys, reqPages);
  pmm_free_frame(respPhys);
  return ret;
}

int virtioGpuTransfer(VirtioGpuDevice *gpu, const NagTransferArgs *args, uint64_t *outFence) {
  if (!gpu || !args || args->resourceId == 0) return -1;

  uint64_t rflags = spin_lock_irqsave(&gpu->resLock);
  VirtioGpuResource *curr = gpu->resources;
  int found = 0;
  while (curr) {
    if (curr->resourceId == args->resourceId) {
      found = 1;
      break;
    }
    curr = curr->next;
  }
  spin_unlock_irqrestore(&gpu->resLock, rflags);

  if (!found) return -1;

  uint64_t fflags = spin_lock_irqsave(&gpu->fenceLock);
  uint64_t fence = gpu->nextFenceId++;
  spin_unlock_irqrestore(&gpu->fenceLock, fflags);

  VirtioGpuTransferHost3d req;
  memset(&req, 0, sizeof(req));
  req.hdr.type = (args->direction == NAG_TRANSFER_TO_HOST) ?
                 VIRTIO_GPU_CMD_TRANSFER_TO_HOST_3D :
                 VIRTIO_GPU_CMD_TRANSFER_FROM_HOST_3D;
  req.hdr.flags = VIRTIO_GPU_FLAG_FENCE;
  req.hdr.ctxId = args->contextId;
  req.hdr.fenceId = fence;

  req.box.x = args->x;
  req.box.y = args->y;
  req.box.z = args->z;
  req.box.w = args->width;
  req.box.h = args->height;
  req.box.d = args->depth ? args->depth : 1;
  req.offset = args->offset;
  req.resourceId = args->resourceId;
  req.level = 0;
  req.stride = 0;
  req.layerStride = 0;

  VirtioGpuCtrlHdr resp;
  memset(&resp, 0, sizeof(resp));

  if (virtioGpuSendControlCmd(gpu, &req, sizeof(req), &resp, sizeof(resp)) != 0 ||
      resp.type != VIRTIO_GPU_RESP_OK_NODATA) {
    return -1;
  }

  fflags = spin_lock_irqsave(&gpu->fenceLock);
  if (fence > gpu->lastCompletedFence) {
    gpu->lastCompletedFence = fence;
  }
  spin_unlock_irqrestore(&gpu->fenceLock, fflags);

  if (outFence) {
    *outFence = fence;
  }
  return 0;
}

// do we move this?
int virtioGpuWaitFence(VirtioGpuDevice *gpu, uint64_t fenceId, uint64_t timeoutMs) {
  if (!gpu) return -1;

  uint64_t fflags = spin_lock_irqsave(&gpu->fenceLock);
  if (gpu->lastCompletedFence >= fenceId) {
    spin_unlock_irqrestore(&gpu->fenceLock, fflags);
    return 0;
  }
  spin_unlock_irqrestore(&gpu->fenceLock, fflags);

  uint64_t start = hpet_get_millis();
  while (1) {
    fflags = spin_lock_irqsave(&gpu->fenceLock);
    if (gpu->lastCompletedFence >= fenceId) {
      spin_unlock_irqrestore(&gpu->fenceLock, fflags);
      return 0;
    }
    spin_unlock_irqrestore(&gpu->fenceLock, fflags);

    if ((hpet_get_millis() - start) >= timeoutMs) {
      return -1;
    }
    __asm__ volatile("pause");
  }
}
