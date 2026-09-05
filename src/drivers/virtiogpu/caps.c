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
  if (!gpu->ctrlDmaVirt || reqLen > 2048 || respLen > 2048) {
    return -1;
  }

  uint8_t *dmaVirt = (uint8_t *)gpu->ctrlDmaVirt;
  uint64_t dmaPhys = (uint64_t)gpu->ctrlDmaPhys;

  memcpy(dmaVirt, req, reqLen);
  memset(dmaVirt + 2048, 0, respLen);

  uint64_t reqPhys = dmaPhys;
  uint64_t respPhys = dmaPhys + 2048;

  if (virtqueueSubmit(gpu->controlQueue, reqPhys, reqLen, respPhys, respLen) != 0) {
    return -1;
  }

  virtqueueKick(gpu->controlQueue);

  if (virtqueuePoll(gpu->controlQueue, 1000) != 0) {
    return -1;
  }

  memcpy(resp, dmaVirt + 2048, respLen);
  return 0;
}

int virtioGpuGetCapset(VirtioGpuDevice *gpu, uint32_t capsetId, uint32_t capsetVersion, uint32_t capsetMaxSize, void *outCapData, size_t outDataSize) {
  if (capsetMaxSize == 0 || (sizeof(VirtioGpuCtrlHdr) + capsetMaxSize) > 2048) {
    return -1;
  }

  VirtioGpuGetCapset req;
  memset(&req, 0, sizeof(req));
  req.hdr.type = VIRTIO_GPU_CMD_GET_CAPSET;
  req.capsetId = capsetId;
  req.capsetVersion = capsetVersion;

  uint32_t respTotalLen = sizeof(VirtioGpuCtrlHdr) + capsetMaxSize;
  uint8_t respBuf[2048];
  memset(respBuf, 0, respTotalLen);

  if (virtioGpuSendControlCmd(gpu, &req, sizeof(req), respBuf, respTotalLen) != 0) {
    return -1;
  }

  VirtioGpuRespCapset *resp = (VirtioGpuRespCapset *)respBuf;
  if (resp->hdr.type != VIRTIO_GPU_RESP_OK_CAPSET) {
    kprintf("[VIRTIO-GPU] GET_CAPSET failed with error 0x%04x\n", resp->hdr.type);
    return -1;
  }

  size_t copyBytes = capsetMaxSize < outDataSize ? capsetMaxSize : outDataSize;
  memcpy(outCapData, resp->capsetData, copyBytes);
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
        }
      }
    } else if (respInfo.capsetId == VIRTIO_GPU_CAPSET_VIRGL || respInfo.capsetId == VIRTIO_GPU_CAPSET_VIRGL2) {
      gpu->hasVirgl = 1;
    }
  }
}
