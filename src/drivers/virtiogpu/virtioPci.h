#pragma once
#include <stdint.h>

#define VIRTIO_PCI_VENDOR_ID 0x1AF4
#define VIRTIO_PCI_DEVICE_ID 0x1050

#define VIRTIO_PCI_CAP_COMMON_CFG 1
#define VIRTIO_PCI_CAP_NOTIFY_CFG 2
#define VIRTIO_PCI_CAP_ISR_CFG 3
#define VIRTIO_PCI_CAP_DEVICE_CFG 4
#define VIRTIO_PCI_CAP_PCI_CFG 5
#define VIRTIO_PCI_CAP_SHARED_MEMORY_CFG 8

#define VIRTIO_STATUS_ACKNOWLEDGE 1
#define VIRTIO_STATUS_DRIVER 2
#define VIRTIO_STATUS_DRIVER_OK 4
#define VIRTIO_STATUS_FEATURES_OK 8
#define VIRTIO_STATUS_FAILED 128

#define VIRTIO_GPU_F_VIRGL (1ULL << 0)
#define VIRTIO_GPU_F_EDID (1ULL << 1)
#define VIRTIO_GPU_F_RESOURCE_UUID (1ULL << 2)
#define VIRTIO_GPU_F_RESOURCE_BLOB (1ULL << 3)
#define VIRTIO_GPU_F_CONTEXT_INIT (1ULL << 4)

#define VIRTIO_F_VERSION_1 (1ULL << 32)

typedef struct {
  uint8_t capVndr;
  uint8_t capNext;
  uint8_t capLen;
  uint8_t cfgType;
  uint8_t bar;
  uint8_t padding[3];
  uint32_t offset;
  uint32_t length;
} __attribute__((packed)) VirtioPciCap;

typedef struct {
  VirtioPciCap cap;
  uint32_t notifyOffMultiplier;
} __attribute__((packed)) VirtioPciNotifyCap;

typedef struct {
  volatile uint32_t deviceFeatureSelect;
  volatile uint32_t deviceFeature;
  volatile uint32_t driverFeatureSelect;
  volatile uint32_t driverFeature;
  volatile uint16_t msixConfig;
  volatile uint16_t numQueues;
  volatile uint8_t deviceStatus;
  volatile uint8_t configGeneration;
  volatile uint16_t queueSelect;
  volatile uint16_t queueSize;
  volatile uint16_t queueMsixVector;
  volatile uint16_t queueEnable;
  volatile uint16_t queueNotifyOff;
  volatile uint64_t queueDesc;
  volatile uint64_t queueDriver;
  volatile uint64_t queueDevice;
} __attribute__((packed)) VirtioPciCommonCfg;

typedef struct {
  volatile uint32_t eventsRead;
  volatile uint32_t eventsClear;
  volatile uint32_t numScanouts;
  volatile uint32_t numCapsets;
} __attribute__((packed)) VirtioGpuConfig;
