#pragma once

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#include <novamod.h>
#include <nv_device.h>
#include <nv_falcon.h>
#include <nv_dma.h>
#include <nv_gsp.h>
#include <nv_wpr.h>


#define NV_SEC2_BASE 0x00840000U
#define NV_SEC2_FBIF_BASE 0x00840600U

#define NV_PSEC_FALCON_ENGINE_RESET_REG 0x008403C0U
#define NV_PSEC_FALCON_ENGINE_RESET_VAL 0x00000001U

#define NV_SEC2_BOOTER_SUCCESS 0x00000000U
#define NV_SEC2_BOOTER_ERROR_NOT_STARTED 0xFFFFFFFFU

typedef struct {
  uint32_t reserved[4];
  uint32_t signature[4];
  uint32_t ctx_dma;
  uint64_t code_dma_base;
  uint32_t non_sec_code_off;
  uint32_t non_sec_code_size;
  uint32_t sec_code_off;
  uint32_t sec_code_size;
  uint32_t code_entry_point;
  uint64_t data_dma_base;
  uint32_t data_size;
  uint32_t argc;
  uint32_t argv;
} __attribute__((packed)) NvSec2BlDmemDesc;

typedef struct {
  NvFalcon falcon; // Base Falcon controller
  NvDmaBuffer ucode_fw; // Staged booter executable image DMA buffer
  uint32_t ucode_size; // Size of booter ucode
  uint32_t imem_size; // IMEM size from HWCFG
} NvSec2Context;

int nv_sec2_init(const NvDevice *dev, NvSec2Context *sec2);
int nv_sec2_stage_booter(NvSec2Context *sec2);
int nv_sec2_execute_booter_load(const NvDevice *dev, const NvSec2Context *sec2, const NvGspContext *gsp);
void nv_sec2_cleanup(NvSec2Context *sec2);
