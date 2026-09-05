#pragma once
#include <stdint.h>

#define VIRTIO_GPU_CMD_GET_CAPSET_INFO 0x0108
#define VIRTIO_GPU_RESP_OK_CAPSET_INFO 0x1102

#define VIRTIO_GPU_CMD_GET_CAPSET 0x0109
#define VIRTIO_GPU_RESP_OK_CAPSET 0x1103

#define VIRTIO_GPU_CAPSET_VIRGL 1
#define VIRTIO_GPU_CAPSET_VIRGL2 2
#define VIRTIO_GPU_CAPSET_VENUS 4

typedef struct {
  uint32_t type;
  uint32_t flags;
  uint64_t fenceId;
  uint32_t ctxId;
  uint8_t ringIdx;
  uint8_t padding[3];
} __attribute__((packed)) VirtioGpuCtrlHdr;

typedef struct {
  VirtioGpuCtrlHdr hdr;
  uint32_t capsetIndex;
  uint32_t padding;
} __attribute__((packed)) VirtioGpuGetCapsetInfo;

typedef struct {
  VirtioGpuCtrlHdr hdr;
  uint32_t capsetId;
  uint32_t capsetMaxVersion;
  uint32_t capsetMaxSize;
  uint32_t padding;
} __attribute__((packed)) VirtioGpuRespCapsetInfo;

typedef struct {
  VirtioGpuCtrlHdr hdr;
  uint32_t capsetId;
  uint32_t capsetVersion;
} __attribute__((packed)) VirtioGpuGetCapset;

typedef struct {
  VirtioGpuCtrlHdr hdr;
  uint8_t capsetData[];
} __attribute__((packed)) VirtioGpuRespCapset;

typedef struct {
  uint32_t wireFormatVersion;
  uint32_t vkXmlVersion;
  uint32_t vkExtCommandSerializationSpecVersion;
  uint32_t vkMesaVenusProtocolSpecVersion;
} __attribute__((packed)) VirtioGpuCapsetVenus;
