#include "pmm.h"
#include <efi.h>

static uint64_t *g_bitmap = NULL;
static uint64_t g_total_frames = 0;
static uint64_t g_used_frames = 0;
static uint64_t g_bitmap_words = 0;
static uint64_t g_last_scanned_index = 0;

static inline void bitmap_set_bit(uint64_t frame_idx) {
  g_bitmap[frame_idx / 64] |= (1ULL << (frame_idx % 64));
}

static inline void bitmap_clear_bit(uint64_t frame_idx) {
  g_bitmap[frame_idx / 64] &= ~(1ULL << (frame_idx % 64));
}

static inline int bitmap_test_bit(uint64_t frame_idx) {
  return (g_bitmap[frame_idx / 64] & (1ULL << (frame_idx % 64))) != 0;
}

static void pmm_reserve_range(uint64_t phys_base, uint64_t size) {
  uint64_t start_frame = phys_base / PAGE_SIZE;
  uint64_t end_frame = (phys_base + size + PAGE_SIZE - 1) / PAGE_SIZE;

  for (uint64_t f = start_frame; f < end_frame && f < g_total_frames; f++) {
    if (!bitmap_test_bit(f)) {
      bitmap_set_bit(f);
      g_used_frames++;
    }
  }
}

static inline int is_ram_type(uint32_t type) {
  return type == EfiConventionalMemory ||
  type == EfiBootServicesCode ||
  type == EfiBootServicesData ||
  type == EfiLoaderCode ||
  type == EfiLoaderData ||
  type == EfiACPIReclaimMemory ||
  type == EfiACPIMemoryNVS ||
  type == EfiRuntimeServicesCode ||
  type == EfiRuntimeServicesData;
}

void pmm_init(BootInfo *boot_info) {
  uint8_t *map_bytes = (uint8_t *)boot_info->memory_map.map;
  uint64_t desc_size = boot_info->memory_map.descriptor_size;
  uint64_t num_entries = boot_info->memory_map.size / desc_size;

  uint64_t highest_phys = 0;
  for (uint64_t i = 0; i < num_entries; i++) {
    EFI_MEMORY_DESCRIPTOR *desc = (EFI_MEMORY_DESCRIPTOR *)(map_bytes + (i * desc_size));
    if (!is_ram_type(desc->Type)) {
      continue;
    }
    uint64_t end = desc->PhysicalStart + (desc->NumberOfPages * PAGE_SIZE);
    if (end > highest_phys) {
      highest_phys = end;
    }
  }

  g_total_frames = highest_phys / PAGE_SIZE;
  uint64_t bitmap_size_bytes = (g_total_frames + 7) / 8;
  uint64_t bitmap_pages = (bitmap_size_bytes + PAGE_SIZE - 1) / PAGE_SIZE;
  g_bitmap_words = (bitmap_size_bytes + sizeof(uint64_t) - 1) / sizeof(uint64_t);

  uint64_t bitmap_phys = 0;
  for (uint64_t i = 0; i < num_entries; i++) {
    EFI_MEMORY_DESCRIPTOR *desc = (EFI_MEMORY_DESCRIPTOR *)(map_bytes + (i * desc_size));
    if (desc->Type == EfiConventionalMemory && desc->NumberOfPages >= bitmap_pages) {
      if (desc->PhysicalStart >= 0x100000ULL) {
        bitmap_phys = desc->PhysicalStart;
        break;
      }
    }
  }

  if (!bitmap_phys) {
    for (uint64_t i = 0; i < num_entries; i++) {
      EFI_MEMORY_DESCRIPTOR *desc = (EFI_MEMORY_DESCRIPTOR *)(map_bytes + (i * desc_size));
      if (desc->Type == EfiConventionalMemory && desc->NumberOfPages >= bitmap_pages) {
        bitmap_phys = desc->PhysicalStart;
        break;
      }
    }
  }

  g_bitmap = (uint64_t *)(bitmap_phys + HHDM_BASE);

  for (uint64_t i = 0; i < g_bitmap_words; i++) {
    g_bitmap[i] = ~0ULL;
  }
  g_used_frames = g_total_frames;

  for (uint64_t i = 0; i < num_entries; i++) {
    EFI_MEMORY_DESCRIPTOR *desc = (EFI_MEMORY_DESCRIPTOR *)(map_bytes + (i * desc_size));
    if (desc->Type == EfiConventionalMemory) { // crows we can reclaim EfiBootServicesCode and EfiBootServicesData if we wanna
      uint64_t start_frame = desc->PhysicalStart / PAGE_SIZE;
      uint64_t end_frame = start_frame + desc->NumberOfPages;

      for (uint64_t f = start_frame; f < end_frame && f < g_total_frames; f++) {
        if (bitmap_test_bit(f)) {
          bitmap_clear_bit(f);
          g_used_frames--;
        }
      }
    }
  }

  pmm_reserve_range(0x0, 0x100000ULL);
  pmm_reserve_range(bitmap_phys, bitmap_pages * PAGE_SIZE);
  pmm_reserve_range(boot_info->kernel.phys_base, boot_info->kernel.size);
  pmm_reserve_range(boot_info->stack.phys_base, boot_info->stack.size);
  pmm_reserve_range((uint64_t)boot_info - HHDM_BASE, PAGE_SIZE);
  pmm_reserve_range((uint64_t)boot_info->memory_map.map - HHDM_BASE, boot_info->memory_map.size);
  pmm_reserve_range((uint64_t)boot_info->pml4 - HHDM_BASE, PAGE_SIZE);

  if (boot_info->font.header) {
    pmm_reserve_range((uint64_t)boot_info->font.header - HHDM_BASE, PAGE_SIZE * 4);
  }

  g_last_scanned_index = 0;
}

void *pmm_alloc_frame(void) {
  for (uint64_t i = g_last_scanned_index; i < g_bitmap_words; i++) {
    if (g_bitmap[i] != ~0ULL) {
      int bit = __builtin_ctzll(~g_bitmap[i]);
      uint64_t frame_idx = (i * 64) + (uint64_t)bit;

      if (frame_idx >= g_total_frames) {
        return NULL;
      }

      bitmap_set_bit(frame_idx);
      g_used_frames++;
      g_last_scanned_index = i;
      return (void *)(frame_idx * PAGE_SIZE);
    }
  }

  for (uint64_t i = 0; i < g_last_scanned_index; i++) {
    if (g_bitmap[i] != ~0ULL) {
      int bit = __builtin_ctzll(~g_bitmap[i]);
      uint64_t frame_idx = (i * 64) + (uint64_t)bit;

      if (frame_idx >= g_total_frames) {
        return NULL;
      }

      bitmap_set_bit(frame_idx);
      g_used_frames++;
      g_last_scanned_index = i;
      return (void *)(frame_idx * PAGE_SIZE);
    }
  }

  return NULL;
}

void *pmm_alloc_frames(size_t count) {
  if (count == 0) {
    return NULL;
  }
  if (count == 1) {
    return pmm_alloc_frame();
  }

  uint64_t contiguous_found = 0;
  uint64_t start_frame = 0;

  for (uint64_t i = 0; i < g_total_frames; i++) {
    if (bitmap_test_bit(i)) {
      contiguous_found = 0;
      start_frame = i + 1;
      continue;
    }

    contiguous_found++;
    if (contiguous_found == count) {
      for (uint64_t f = start_frame; f < start_frame + count; f++) {
        bitmap_set_bit(f);
      }
      g_used_frames += count;
      return (void *)(start_frame * PAGE_SIZE);
    }
  }

  return NULL;
}

void pmm_free_frame(void *phys_addr) {
  uint64_t addr = (uint64_t)phys_addr;
  if (!addr || (addr % PAGE_SIZE) != 0) {
    return;
  }

  uint64_t frame_idx = addr / PAGE_SIZE;
  if (frame_idx >= g_total_frames) {
    return;
  }

  if (bitmap_test_bit(frame_idx)) {
    bitmap_clear_bit(frame_idx);
    g_used_frames--;

    uint64_t word_idx = frame_idx / 64;
    if (word_idx < g_last_scanned_index) {
      g_last_scanned_index = word_idx;
    }
  }
}

void pmm_free_frames(void *phys_addr, size_t count) {
  uint64_t addr = (uint64_t)phys_addr;
  if (!addr || (addr % PAGE_SIZE) != 0 || count == 0) {
    return;
  }

  uint64_t start_frame = addr / PAGE_SIZE;
  uint64_t end_frame = start_frame + count;

  if (end_frame > g_total_frames) {
    end_frame = g_total_frames;
  }

  for (uint64_t f = start_frame; f < end_frame; f++) {
    if (bitmap_test_bit(f)) {
      bitmap_clear_bit(f);
      g_used_frames--;
    }
  }

  uint64_t word_idx = start_frame / 64;
  if (word_idx < g_last_scanned_index) {
    g_last_scanned_index = word_idx;
  }
}

uint64_t pmm_get_total_frames(void) {
  return g_total_frames;
}

uint64_t pmm_get_free_frames(void) {
  return g_total_frames - g_used_frames;
}

uint64_t pmm_get_used_frames(void) {
  return g_used_frames;
}
