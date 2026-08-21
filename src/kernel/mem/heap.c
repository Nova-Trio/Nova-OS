#include "heap.h"
#include "vmm.h"
#include "pmm.h"

#define SLAB_MAGIC 0x51AB0001U
#define LARGE_MAGIC 0x1A26E001U

#define NUM_SIZE_CLASSES 8
#define MAX_SLAB_SIZE 2048

static const size_t g_size_classes[NUM_SIZE_CLASSES] = {
  16, 32, 64, 128, 256, 512, 1024, 2048
};

typedef struct Slab {
  uint32_t magic;
  uint32_t object_size;
  uint32_t total_objects;
  uint32_t free_objects;
  void *free_list;
  struct Slab *next;
  struct Slab *prev;
  uint8_t padding[24];
} __attribute__((aligned(64))) Slab;

typedef struct {
  size_t object_size;
  Slab *slabs;
} SlabCache;

typedef struct {
  uint32_t magic;
  uint32_t num_pages;
  size_t user_size;
  uint8_t padding[48];
} __attribute__((aligned(64))) LargeAllocHeader;

typedef struct VirtualFreeNode {
  uint64_t virt_addr;
  size_t page_count;
  struct VirtualFreeNode *next;
} VirtualFreeNode;

static SlabCache g_caches[NUM_SIZE_CLASSES];
static uint64_t g_heap_virtual_cursor = KERNEL_HEAP_BASE;
static VirtualFreeNode *g_free_virtual_ranges = NULL;

static void recycle_virtual_range(uint64_t virt_addr, size_t page_count) {
  VirtualFreeNode *node = (VirtualFreeNode*)kmalloc(sizeof(VirtualFreeNode));
  if (!node) {
    return;
  }

  node->virt_addr = virt_addr;
  node->page_count = page_count;
  node->next = NULL;

  VirtualFreeNode **curr = &g_free_virtual_ranges;
  while (*curr && (*curr)->virt_addr < virt_addr) {
    curr = &(*curr)->next;
  }

  node->next = *curr;
  *curr = node;

  VirtualFreeNode *it = g_free_virtual_ranges;
  while (it && it->next) {
    if (it->virt_addr + (it->page_count * PAGE_SIZE) == it->next->virt_addr) {
      VirtualFreeNode* merged = it->next;
      it->page_count += merged->page_count;
      it->next = merged->next;
      kfree(merged);
    } else {
      it = it->next;
    }
  }
}

static void *alloc_virtual_pages(size_t count) {
  PageDirectory kernel_pml4 = vmm_get_kernel_pml4();
  void *virt_base = NULL;

  VirtualFreeNode **curr = &g_free_virtual_ranges;
  while (*curr) {
    if ((*curr)->page_count == count) {
      VirtualFreeNode *node = *curr;
      virt_base = (void *)node->virt_addr;
      *curr = node->next;
      kfree(node);
      break;
    } else if ((*curr)->page_count > count) {
      VirtualFreeNode *node = *curr;
      virt_base = (void *)node->virt_addr;
      node->virt_addr += count * PAGE_SIZE;
      node->page_count -= count;
      break;
    }
    curr = &(*curr)->next;
  }

  if (!virt_base) {
    virt_base = (void *)g_heap_virtual_cursor;
    g_heap_virtual_cursor += count * PAGE_SIZE;
  }

  for (size_t i = 0; i < count; i++) {
    void *frame = pmm_alloc_frame();
    if (!frame) {
      for (size_t j = 0; j < i; j++) {
        uint64_t v = (uint64_t)virt_base + (j * PAGE_SIZE);
        uint64_t ph = vmm_virt_to_phys(kernel_pml4, v);
        vmm_unmap_page(kernel_pml4, v);
        if (ph) pmm_free_frame((void *)ph);
      }
      recycle_virtual_range((uint64_t)virt_base, count);
      return NULL;
    }

    uint64_t v = (uint64_t)virt_base + (i * PAGE_SIZE);
    if (vmm_map_page(kernel_pml4, v, (uint64_t)frame, VMM_FLAG_PRESENT | VMM_FLAG_WRITABLE) != 0) {
      pmm_free_frame(frame);
      for (size_t j = 0; j < i; j++) {
        uint64_t v_prev = (uint64_t)virt_base + (j * PAGE_SIZE);
        uint64_t ph = vmm_virt_to_phys(kernel_pml4, v_prev);
        vmm_unmap_page(kernel_pml4, v_prev);
        if (ph) pmm_free_frame((void *)ph);
      }
      recycle_virtual_range((uint64_t)virt_base, count);
      return NULL;
    }
  }

  return virt_base;
}

static Slab *create_slab(size_t object_size) {
  void *page = alloc_virtual_pages(1);
  if (!page) {
    return NULL;
  }

  Slab *slab = (Slab *)page;
  slab->magic = SLAB_MAGIC;
  slab->object_size = (uint32_t)object_size;
  slab->next = NULL;
  slab->prev = NULL;

  uintptr_t data_start = ((uintptr_t)page + sizeof(Slab) + object_size - 1) & ~(object_size - 1);
  uint32_t capacity = (uint32_t)((PAGE_SIZE - (data_start - (uintptr_t)page)) / object_size);

  slab->total_objects = capacity;
  slab->free_objects = capacity;

  uint8_t *curr = (uint8_t *)data_start;
  for (uint32_t i = 0; i < capacity - 1; i++) {
    uint8_t *next = curr + object_size;
    *(void **)curr = (void *)next;
    curr = next;
  }
  *(void **)curr = NULL;

  slab->free_list = (void *)data_start;
  return slab;
}

void heap_init(void) {
  g_heap_virtual_cursor = KERNEL_HEAP_BASE;
  g_free_virtual_ranges = NULL;

  for (size_t i = 0; i < NUM_SIZE_CLASSES; i++) {
    g_caches[i].object_size = g_size_classes[i];
    g_caches[i].slabs = NULL;
  }
}

void *kmalloc(size_t size) {
  if (size == 0) {
    return NULL;
  }

  if (size <= MAX_SLAB_SIZE) {
    size_t class_idx = 0;
    while (class_idx < NUM_SIZE_CLASSES && g_size_classes[class_idx] < size) {
      class_idx++;
    }

    SlabCache *cache = &g_caches[class_idx];
    Slab *slab = cache->slabs;

    while (slab && slab->free_objects == 0) {
      slab = slab->next;
    }

    if (!slab) {
      slab = create_slab(cache->object_size);
      if (!slab) {
        return NULL;
      }

      slab->next = cache->slabs;
      if (cache->slabs) {
        cache->slabs->prev = slab;
      }
      cache->slabs = slab;
    }

    void *obj = slab->free_list;
    slab->free_list = *(void **)obj;
    slab->free_objects--;

    return obj;
  }

  size_t total_size = sizeof(LargeAllocHeader) + size;
  size_t pages = (total_size + PAGE_SIZE - 1) / PAGE_SIZE;

  void *virt = alloc_virtual_pages(pages);
  if (!virt) {
    return NULL;
  }

  LargeAllocHeader *hdr = (LargeAllocHeader *)virt;
  hdr->magic = LARGE_MAGIC;
  hdr->num_pages = (uint32_t)pages;
  hdr->user_size = size;

  return (void *)((uintptr_t)virt + sizeof(LargeAllocHeader));
}

void kfree(void *ptr) {
  if (!ptr) {
    return;
  }

  uintptr_t page_base = (uintptr_t)ptr & ~(PAGE_SIZE - 1);
  uint32_t magic = *(uint32_t *)page_base;

  if (magic == SLAB_MAGIC) {
    Slab *slab = (Slab *)page_base;
    *(void **)ptr = slab->free_list;
    slab->free_list = ptr;
    slab->free_objects++;

    if (slab->free_objects == slab->total_objects && (slab->prev != NULL || slab->next != NULL)) {
      size_t class_idx = 0;
      while (class_idx < NUM_SIZE_CLASSES && g_size_classes[class_idx] != slab->object_size) {
        class_idx++;
      }

      if (class_idx < NUM_SIZE_CLASSES) {
        SlabCache *cache = &g_caches[class_idx];

        if (slab->prev) {
          slab->prev->next = slab->next;
        } else {
          cache->slabs = slab->next;
        }

        if (slab->next) {
          slab->next->prev = slab->prev;
        }

        PageDirectory kernel_pml4 = vmm_get_kernel_pml4();
        uint64_t phys = vmm_virt_to_phys(kernel_pml4, page_base);

        vmm_unmap_page(kernel_pml4, page_base);
        if (phys) {
          pmm_free_frame((void *)phys);
        }

        recycle_virtual_range(page_base, 1);
      }
    }
    return;
  }

  if (magic == LARGE_MAGIC) {
    LargeAllocHeader *hdr = (LargeAllocHeader *)page_base;
    uint32_t pages = hdr->num_pages;
    PageDirectory kernel_pml4 = vmm_get_kernel_pml4();

    for (uint32_t i = 0; i < pages; i++) {
      uint64_t v = page_base + (i * PAGE_SIZE);
      uint64_t phys = vmm_virt_to_phys(kernel_pml4, v);

      vmm_unmap_page(kernel_pml4, v);
      if (phys) {
        pmm_free_frame((void *)phys);
      }
    }

    recycle_virtual_range(page_base, pages);
  }
}

void *kzalloc(size_t size) {
  void *ptr = kmalloc(size);
  if (!ptr) {
    return NULL;
  }

  uint8_t *b = (uint8_t *)ptr;
  for (size_t i = 0; i < size; i++) {
    b[i] = 0;
  }

  return ptr;
}

void *krealloc(void *ptr, size_t new_size) {
  if (!ptr) {
    return kmalloc(new_size);
  }

  if (new_size == 0) {
    kfree(ptr);
    return NULL;
  }

  uintptr_t page_base = (uintptr_t)ptr & ~(PAGE_SIZE - 1);
  uint32_t magic = *(uint32_t *)page_base;
  size_t old_size = 0;

  if (magic == SLAB_MAGIC) {
    old_size = ((Slab *)page_base)->object_size;
  } else if (magic == LARGE_MAGIC) {
    old_size = ((LargeAllocHeader *)page_base)->user_size;
  } else {
    return NULL;
  }

  if (new_size <= old_size) {
    return ptr;
  }

  void *new_ptr = kmalloc(new_size);
  if (!new_ptr) {
    return NULL;
  }

  uint8_t *dst = (uint8_t *)new_ptr;
  const uint8_t *src = (const uint8_t *)ptr;
  for (size_t i = 0; i < old_size; i++) {
    dst[i] = src[i];
  }

  kfree(ptr);
  return new_ptr;
}
