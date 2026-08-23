#pragma once
#include <novamod.h>

typedef struct {
  uint64_t phys_addr;
  uint64_t virt_addr;
  size_t size;
  size_t page_count;
} NvDmaBuffer;

typedef struct {
  NvDmaBuffer *buf;
  size_t offset;
  size_t capacity;
} NvDmaWriter;

static inline void nv_dma_wmb(void) {
  __asm__ volatile("sfence" ::: "memory");
}

static inline void nv_dma_rmb(void) {
  __asm__ volatile("lfence" ::: "memory");
}

static inline void nv_dma_mb(void) {
  __asm__ volatile("mfence" ::: "memory");
}

static inline void nv_dma_wr32(const NvDmaBuffer *buf, size_t offset, uint32_t val) {
  if (buf && buf->virt_addr && (offset + sizeof(uint32_t) <= buf->size)) {
    *(volatile uint32_t *)(buf->virt_addr + offset) = val;
  }
}

static inline uint32_t nv_dma_rd32(const NvDmaBuffer *buf, size_t offset) {
  if (buf && buf->virt_addr && (offset + sizeof(uint32_t) <= buf->size)) {
    return *(volatile uint32_t *)(buf->virt_addr + offset);
  }
  return 0;
}

static inline void nv_dma_wr64(const NvDmaBuffer *buf, size_t offset, uint64_t val) {
  if (buf && buf->virt_addr && (offset + sizeof(uint64_t) <= buf->size)) {
    *(volatile uint64_t *)(buf->virt_addr + offset) = val;
  }
}

static inline uint64_t nv_dma_rd64(const NvDmaBuffer *buf, size_t offset) {
  if (buf && buf->virt_addr && (offset + sizeof(uint64_t) <= buf->size)) {
    return *(volatile uint64_t *)(buf->virt_addr + offset);
  }
  return 0;
}

int nv_dma_alloc(NvDmaBuffer *buf, size_t size);
void nv_dma_free(NvDmaBuffer *buf);

void nv_dma_writer_init(NvDmaWriter *w, NvDmaBuffer *buf);
int nv_dma_push(NvDmaWriter *w, uint32_t val);
int nv_dma_push64(NvDmaWriter *w, uint64_t val);
void nv_dma_writer_flush(NvDmaWriter *w);
