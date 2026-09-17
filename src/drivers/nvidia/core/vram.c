#include <nv_device.h>
#include <novamod.h>
#include <stdbool.h>

int nvVramInit(NvDevice *dev, uint64_t phys_base, uint64_t size) {
  if (!dev || size == 0) return -1;

  uint64_t total_pages = size / NV_VRAM_PAGE_SIZE;
  size_t bitmap_words = (total_pages + 63ULL) / 64ULL;
  size_t bitmap_bytes = bitmap_words * sizeof(uint64_t);

  uint64_t *bitmap = (uint64_t *)kzalloc(bitmap_bytes);
  if (!bitmap) {
    kprintf("[NV/VRAM] Error: Failed to allocate %u KB for VRAM bitmap\n", (uint32_t)(bitmap_bytes / 1024));
    return -1;
  }

  dev->vram.physBase = phys_base;
  dev->vram.size = size;
  dev->vram.usableLimit = phys_base + size - 1;
  dev->vram.freeBytes = size;
  dev->vram.totalPages = total_pages;
  dev->vram.bitmap = bitmap;
  dev->vram.bitmapWords = bitmap_words;

  kprintf("[NV/VRAM] Allocator initialized: %llu MB usable (%llu pages, bitmap: %u KB)\n", size / (1024ULL * 1024ULL), total_pages, (uint32_t)(bitmap_bytes / 1024));

  return 0;
}

void nvVramCleanup(NvDevice *dev) {
  if (!dev || !dev->vram.bitmap) return;

  kfree(dev->vram.bitmap);
  dev->vram.bitmap = NULL;
  dev->vram.physBase = 0;
  dev->vram.size = 0;
  dev->vram.usableLimit = 0;
  dev->vram.freeBytes = 0;
  dev->vram.totalPages = 0;
  dev->vram.bitmapWords = 0;
}

int nvVramAlloc(NvDevice *dev, size_t size, uint64_t align, uint64_t *out_phys) {
  if (!dev || !dev->vram.bitmap || size == 0 || !out_phys) return -1;

  if (align < NV_VRAM_PAGE_SIZE) align = NV_VRAM_PAGE_SIZE;

  uint64_t num_pages = (size + NV_VRAM_PAGE_SIZE - 1) / NV_VRAM_PAGE_SIZE;
  uint64_t align_pages = align / NV_VRAM_PAGE_SIZE;

  uint64_t initial_offset = 0;
  if ((dev->vram.physBase % align) != 0) {
    uint64_t next_aligned_phys = (dev->vram.physBase + align - 1) & ~(align - 1);
    initial_offset = (next_aligned_phys - dev->vram.physBase) / NV_VRAM_PAGE_SIZE;
  }

  uint64_t total_pages = dev->vram.totalPages;
  uint64_t found_idx = (uint64_t)-1;

  for (uint64_t i = initial_offset; i + num_pages <= total_pages; ) {
    if (align_pages == 1 && (i % 64 == 0) && (dev->vram.bitmap[i / 64] == ~0ULL)) {
      i += 64;
      continue;
    }

    if (dev->vram.bitmap[i / 64] & (1ULL << (i % 64))) {
      i += align_pages;
      continue;
    }

    bool fits = true;
    uint64_t conflict_page = i;
    for (uint64_t j = 0; j < num_pages; j++) {
      uint64_t cur_page = i + j;
      if (dev->vram.bitmap[cur_page / 64] & (1ULL << (cur_page % 64))) {
        fits = false;
        conflict_page = cur_page;
        break;
      }
    }

    if (fits) {
      found_idx = i;
      break;
    }

    uint64_t next_i = conflict_page + 1;
    if (next_i <= i) next_i = i + 1;
    uint64_t rem = (next_i >= initial_offset) ? ((next_i - initial_offset) % align_pages) : 0;
    if (rem != 0) {
      next_i += (align_pages - rem);
    }
    i = next_i;
  }

  if (found_idx == (uint64_t)-1) {
    kprintf("[NV/VRAM] Error: Out of VRAM (requested %llu KB, align %llu KB, free %llu MB)\n", (uint64_t)(size / 1024), (uint64_t)(align / 1024), dev->vram.freeBytes / (1024ULL * 1024ULL));
    return -1;
  }

  for (uint64_t j = 0; j < num_pages; j++) {
    uint64_t page = found_idx + j;
    dev->vram.bitmap[page / 64] |= (1ULL << (page % 64));
  }

  dev->vram.freeBytes -= (num_pages * NV_VRAM_PAGE_SIZE);
  *out_phys = dev->vram.physBase + (found_idx * NV_VRAM_PAGE_SIZE);
  return 0;
}

void nvVramFree(NvDevice *dev, uint64_t phys_addr, size_t size) {
  if (!dev || !dev->vram.bitmap || size == 0) return;

  if (phys_addr < dev->vram.physBase || (phys_addr + size - 1) > dev->vram.usableLimit) {
    kprintf("[NV/VRAM] Error: Address 0x%016llx out of VRAM bounds!\n", phys_addr);
    return;
  }

  uint64_t start_page = (phys_addr - dev->vram.physBase) / NV_VRAM_PAGE_SIZE;
  uint64_t num_pages = (size + NV_VRAM_PAGE_SIZE - 1) / NV_VRAM_PAGE_SIZE;

  for (uint64_t j = 0; j < num_pages; j++) {
    uint64_t page = start_page + j;
    if ((dev->vram.bitmap[page / 64] & (1ULL << (page % 64))) == 0) {
      kprintf("[NV/VRAM] Warning: Possible double free at page %llu (PA: 0x%016llx)\n", page, dev->vram.physBase + (page * NV_VRAM_PAGE_SIZE));
    }
    dev->vram.bitmap[page / 64] &= ~(1ULL << (page % 64));
  }

  dev->vram.freeBytes += (num_pages * NV_VRAM_PAGE_SIZE);
}


void nvVramDump(const NvDevice *dev) {
  if (!dev) return;
  kprintf("[NV/VRAM] Pool: [0x%016llx - 0x%016llx] | Total: %llu MB | Free: %llu MB\n", dev->vram.physBase, dev->vram.usableLimit, dev->vram.size / (1024ULL * 1024ULL),
          dev->vram.freeBytes / (1024ULL * 1024ULL));
}
