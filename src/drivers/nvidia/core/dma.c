#include <nv_dma.h>

int nv_dma_alloc(NvDmaBuffer *buf, size_t size) {
  if (!buf || size == 0) {
    return -1;
  }

  size_t page_count = (size + PAGE_SIZE - 1) / PAGE_SIZE;
  void *phys = pmm_alloc_frames(page_count);
  if (!phys) {
    return -1;
  }

  uint64_t phys_addr = (uint64_t)phys;
  uint64_t virt_addr = phys_addr + HHDM_BASE;

  buf->phys_addr = phys_addr;
  buf->virt_addr = virt_addr;
  buf->size = size;
  buf->page_count = page_count;

  memset((void *)virt_addr, 0, page_count * PAGE_SIZE);
  return 0;
}

void nv_dma_free(NvDmaBuffer *buf) {
  if (!buf || !buf->phys_addr || buf->page_count == 0) {
    return;
  }

  pmm_free_frames((void *)buf->phys_addr, buf->page_count);
  buf->phys_addr = 0;
  buf->virt_addr = 0;
  buf->size = 0;
  buf->page_count = 0;
}

void nv_dma_writer_init(NvDmaWriter *w, NvDmaBuffer *buf) {
  if (!w) {
    return;
  }
  w->buf = buf;
  w->offset = 0;
  w->capacity = buf ? buf->size : 0;
}

int nv_dma_push(NvDmaWriter *w, uint32_t val) {
  if (!w || !w->buf || !w->buf->virt_addr || (w->offset + sizeof(uint32_t) > w->capacity)) {
    return -1;
  }

  *(volatile uint32_t *)(w->buf->virt_addr + w->offset) = val;
  w->offset += sizeof(uint32_t);
  return 0;
}

int nv_dma_push64(NvDmaWriter *w, uint64_t val) {
  if (!w || !w->buf || !w->buf->virt_addr || (w->offset + sizeof(uint64_t) > w->capacity)) {
    return -1;
  }

  *(volatile uint64_t *)(w->buf->virt_addr + w->offset) = val;
  w->offset += sizeof(uint64_t);
  return 0;
}

void nv_dma_writer_flush(NvDmaWriter *w) {
  (void)w;
  nv_dma_wmb();
}
