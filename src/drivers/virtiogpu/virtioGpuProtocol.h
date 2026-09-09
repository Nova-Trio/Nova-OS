#pragma once
#include <stdint.h>

#define VIRTIO_GPU_FLAG_FENCE (1 << 0)

#define VIRTIO_GPU_CMD_RESOURCE_UNREF 0x0102
#define VIRTIO_GPU_CMD_RESOURCE_ATTACH_BACKING 0x0106
#define VIRTIO_GPU_CMD_RESOURCE_DETACH_BACKING 0x0107
#define VIRTIO_GPU_CMD_GET_CAPSET_INFO 0x0108
#define VIRTIO_GPU_CMD_GET_CAPSET 0x0109

#define VIRTIO_GPU_CMD_CTX_CREATE 0x0200
#define VIRTIO_GPU_CMD_CTX_DESTROY 0x0201
#define VIRTIO_GPU_CMD_CTX_ATTACH_RESOURCE 0x0202
#define VIRTIO_GPU_CMD_CTX_DETACH_RESOURCE 0x0203
#define VIRTIO_GPU_CMD_RESOURCE_CREATE_3D 0x0204
#define VIRTIO_GPU_CMD_TRANSFER_TO_HOST_3D 0x0205
#define VIRTIO_GPU_CMD_TRANSFER_FROM_HOST_3D 0x0206
#define VIRTIO_GPU_CMD_SUBMIT_3D 0x0207

#define VIRTIO_GPU_RESP_OK_NODATA 0x1100
#define VIRTIO_GPU_RESP_OK_CAPSET_INFO 0x1102
#define VIRTIO_GPU_RESP_OK_CAPSET 0x1103
#define VIRTIO_GPU_RESP_ERR_UNSPEC 0x1200
#define VIRTIO_GPU_RESP_ERR_OUT_OF_MEMORY 0x1201
#define VIRTIO_GPU_RESP_ERR_INVALID_SCANOUT_ID 0x1202
#define VIRTIO_GPU_RESP_ERR_INVALID_RESOURCE_ID 0x1203
#define VIRTIO_GPU_RESP_ERR_INVALID_CONTEXT_ID 0x1204
#define VIRTIO_GPU_RESP_ERR_INVALID_PARAMETER 0x1205

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

typedef struct {
  VirtioGpuCtrlHdr hdr;
  uint32_t nlen;
  uint32_t padding;
  char debugName[64];
} __attribute__((packed)) VirtioGpuCtxCreate;

typedef struct {
  VirtioGpuCtrlHdr hdr;
} __attribute__((packed)) VirtioGpuCtxDestroy;

typedef struct {
  VirtioGpuCtrlHdr hdr;
  uint32_t resourceId;
  uint32_t padding;
} __attribute__((packed)) VirtioGpuCtxAttachResource;

typedef struct {
  VirtioGpuCtrlHdr hdr;
  uint32_t resourceId;
  uint32_t padding;
} __attribute__((packed)) VirtioGpuCtxDetachResource;

typedef struct {
  VirtioGpuCtrlHdr hdr;
  uint32_t resourceId;
  uint32_t padding;
} __attribute__((packed)) VirtioGpuResourceUnref;

typedef struct {
  uint64_t addr;
  uint32_t length;
  uint32_t padding;
} __attribute__((packed)) VirtioGpuMemEntry;

typedef struct {
  VirtioGpuCtrlHdr hdr;
  uint32_t resourceId;
  uint32_t nrEntries;
} __attribute__((packed)) VirtioGpuResourceAttachBacking;

typedef struct {
  VirtioGpuCtrlHdr hdr;
  uint32_t resourceId;
  uint32_t padding;
} __attribute__((packed)) VirtioGpuResourceDetachBacking;

typedef struct {
  VirtioGpuCtrlHdr hdr;
  uint32_t resourceId;
  uint32_t target;
  uint32_t format;
  uint32_t bind;
  uint32_t width;
  uint32_t height;
  uint32_t depth;
  uint32_t arraySize;
  uint32_t lastLevel;
  uint32_t nrSamples;
  uint32_t flags;
  uint32_t padding;
} __attribute__((packed)) VirtioGpuResourceCreate3d;

typedef struct {
  VirtioGpuCtrlHdr hdr;
  uint32_t size;
  uint32_t padding;
} __attribute__((packed)) VirtioGpuCmdSubmit3d;
