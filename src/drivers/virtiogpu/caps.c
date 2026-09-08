#include <caps.h>
#include <stdint.h>


static int writeUint(char* buf, uint32_t num){
  if(num == 0){
    buf[0] = '0';
    return 1;
  }

  char temp[10];
  int i = 0;

  while(num > 0){
    temp[i++] = '0' + (num % 10);
    num /= 10;
  }

  int len = 0;
  for(int j = i - 1; j >= 0; j--){
    buf[len++] = temp[j];
  }
  return len;
}

static void fmtVulkanVersion(uint32_t version, char* outBuf){
  uint32_t major = (version >> 22) & 0x7F;
  uint32_t minor = (version >> 12) & 0x3FF;
  uint32_t patch = version & 0xFFF;

  int i = 0;

  i += writeUint(outBuf + i, major);
  outBuf[i++] = '.';


  i += writeUint(outBuf + i, minor);
  outBuf[i++] = '.';

  i += writeUint(outBuf + i, patch);
  outBuf[i] = '\0';
}

int virtioGpuSendControlCmd(VirtioGpuDevice *gpu, const void *req, uint32_t reqLen, void *resp, uint32_t respLen) {
  if (!gpu || !gpu->controlQueue || !req || reqLen == 0 || !resp || respLen == 0) {
    return -1;
  }

  uint64_t rflags = spin_lock_irqsave(&gpu->ctrlLock);

  uint64_t reqPhys = 0;
  uint64_t respPhys = 0;
  void *reqVirt = NULL;
  void *respVirt = NULL;
  size_t reqPages = 0;
  size_t respPages = 0;
  int dynamicAlloc = 0;

  if (reqLen <= (PAGE_SIZE / 2) && respLen <= (PAGE_SIZE / 2) && gpu->ctrlDmaVirt) {
    reqPhys = (uint64_t)gpu->ctrlDmaPhys;
    respPhys = (uint64_t)gpu->ctrlDmaPhys + (PAGE_SIZE / 2);
    reqVirt = gpu->ctrlDmaVirt;
    respVirt = (void *)((uint8_t *)gpu->ctrlDmaVirt + (PAGE_SIZE / 2));
  } else {
    dynamicAlloc = 1;
    reqPages = (reqLen + PAGE_SIZE - 1) / PAGE_SIZE;
    respPages = (respLen + PAGE_SIZE - 1) / PAGE_SIZE;

    void *reqFrame = pmm_alloc_frames(reqPages);
    void *respFrame = pmm_alloc_frames(respPages);

    if (!reqFrame || !respFrame) {
      if (reqFrame) pmm_free_frames(reqFrame, reqPages);
      if (respFrame) pmm_free_frames(respFrame, respPages);
      spin_unlock_irqrestore(&gpu->ctrlLock, rflags);
      return -1;
    }

    reqPhys = (uint64_t)reqFrame;
    respPhys = (uint64_t)respFrame;
    reqVirt = (void *)(reqPhys + HHDM_BASE);
    respVirt = (void *)(respPhys + HHDM_BASE);
  }

  memcpy(reqVirt, req, reqLen);
  memset(respVirt, 0, respLen);

  VirtqBuf bufs[2] = {
    { .physAddr = reqPhys, .len = reqLen, .write = 0 },
    { .physAddr = respPhys, .len = respLen, .write = 1 }
  };

  int ret = virtqueueSubmitSg(gpu->controlQueue, bufs, 2);
  if (ret == 0) {
    virtqueueKick(gpu->controlQueue);
    ret = virtqueuePoll(gpu->controlQueue, 1000);
    if (ret == 0) {
      memcpy(resp, respVirt, respLen);
    }
  }

  if (dynamicAlloc) {
    pmm_free_frames((void *)reqPhys, reqPages);
    pmm_free_frames((void *)respPhys, respPages);
  }

  spin_unlock_irqrestore(&gpu->ctrlLock, rflags);
  return ret;
}

int virtioGpuGetCapset(VirtioGpuDevice *gpu, uint32_t capsetId, uint32_t capsetVersion, uint32_t capsetMaxSize, void *outCapData, size_t outDataSize) {
  if (!gpu || capsetMaxSize == 0 || !outCapData || outDataSize == 0) {
    return -1;
  }

  uint32_t respTotalLen = sizeof(VirtioGpuCtrlHdr) + capsetMaxSize;
  VirtioGpuRespCapset *resp = (VirtioGpuRespCapset *)kmalloc(respTotalLen);
  if (!resp) {
    return -1;
  }

  VirtioGpuGetCapset req;
  memset(&req, 0, sizeof(req));
  req.hdr.type = VIRTIO_GPU_CMD_GET_CAPSET;
  req.capsetId = capsetId;
  req.capsetVersion = capsetVersion;

  memset(resp, 0, respTotalLen);

  if (virtioGpuSendControlCmd(gpu, &req, sizeof(req), resp, respTotalLen) != 0) {
    kfree(resp);
    return -1;
  }

  if (resp->hdr.type != VIRTIO_GPU_RESP_OK_CAPSET) {
    kprintf("[VIRTIO-GPU] GET_CAPSET failed with error 0x%04x\n", resp->hdr.type);
    kfree(resp);
    return -1;
  }

  size_t copyBytes = capsetMaxSize < outDataSize ? capsetMaxSize : outDataSize;
  memcpy(outCapData, resp->capsetData, copyBytes);
  kfree(resp);
  return 0;
}

void virtioGpuDetectCapsets(VirtioGpuDevice *gpu) {
  gpu->hasVenus = 0;
  gpu->hasVirgl = 0;

  if (!(gpu->negotiatedFeatures & VIRTIO_GPU_F_VIRGL)) {
    return;
  }

  uint32_t numCapsets = gpu->deviceCfg->numCapsets;

  for (uint32_t i = 0; i < numCapsets; i++) {
    VirtioGpuGetCapsetInfo reqInfo;
    memset(&reqInfo, 0, sizeof(reqInfo));
    reqInfo.hdr.type = VIRTIO_GPU_CMD_GET_CAPSET_INFO;
    reqInfo.capsetIndex = i;

    VirtioGpuRespCapsetInfo respInfo;
    memset(&respInfo, 0, sizeof(respInfo));

    if (virtioGpuSendControlCmd(gpu, &reqInfo, sizeof(reqInfo), &respInfo, sizeof(respInfo)) != 0) {
      continue;
    }

    if (respInfo.hdr.type != VIRTIO_GPU_RESP_OK_CAPSET_INFO) {
      continue;
    }

    if (respInfo.capsetId == VIRTIO_GPU_CAPSET_VENUS) {
      VirtioGpuCapsetVenus venusCaps;
      memset(&venusCaps, 0, sizeof(venusCaps));

      if (virtioGpuGetCapset(gpu, VIRTIO_GPU_CAPSET_VENUS, 0, respInfo.capsetMaxSize, &venusCaps, sizeof(venusCaps)) == 0) {

        char vkVersion[8];
        fmtVulkanVersion(venusCaps.vkXmlVersion, vkVersion);

        kprintf("[VIRTIO-GPU] Venus Vulkan Caps: wireVer=%u, vk=%s, protoVer=%u\n",
                venusCaps.wireFormatVersion, vkVersion,
                venusCaps.vkMesaVenusProtocolSpecVersion);

        if (venusCaps.vkXmlVersion > 0) {
          gpu->hasVenus = 1;
          gpu->apiver = venusCaps.vkXmlVersion;
        }
      }
    } else if (respInfo.capsetId == VIRTIO_GPU_CAPSET_VIRGL || respInfo.capsetId == VIRTIO_GPU_CAPSET_VIRGL2) {
      gpu->hasVirgl = 1;
      gpu->apiver = 0x1006000; // OpenGL 4.6; everyone supports it nowdays
    }
  }
}
