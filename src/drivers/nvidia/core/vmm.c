#include <nv_vmm.h>

/*  Returns 1 if entry is 0 */
static int is_table_empty(const NvMmuEntry *table) {
  for (size_t i = 0; i < 512; i++) {
    if (table[i].word0 != 0 || table[i].word1 != 0) {
      return 0;
    }
  }
  return 1;
}

/* Remove a table page from the tracking list and free its physical frame */
static void remove_table_page(NvVmm *vmm, uint64_t phys_addr) {
  NvTablePage **curr = &vmm->table_list;
  while (*curr) {
    if ((*curr)->phys_addr == phys_addr) {
      NvTablePage *to_free = *curr;
      *curr = to_free->next;
      pmm_free_frame((void *)phys_addr);
      kfree(to_free);
      return;
    }
    curr = &(*curr)->next;
  }
}

/*  Get or allocate a page table at the next level
 *
 *  parent_entry : PDE entry that points to this table
 *  aperture     : Where the table is
 *
 *  If the parent_entry already points to a table we return its virtual addr, otherwise we allocate a new zeroed page
*/
static NvMmuEntry *get_or_alloc_table(NvVmm *vmm, NvMmuEntry *parent_entry, NvMmuAperture aperture) {
  uint64_t raw_entry = ((uint64_t)parent_entry->word1 << 32) | parent_entry->word0;

  if ((raw_entry & 0x6ULL) != 0) {
    uint64_t phys = (raw_entry & ~0xFFULL) << 4;
    return (NvMmuEntry *)(phys + HHDM_BASE);
  }

  void *frame = pmm_alloc_frame();
  if (!frame) {
    return NULL;
  }

  uint64_t phys = (uint64_t)frame;
  uint64_t virt = phys + HHDM_BASE;
  memset((void *)virt, 0, NV_VMM_PAGE_SIZE);

  NvTablePage *node = (NvTablePage *)kmalloc(sizeof(NvTablePage));
  if (node) {
    node->phys_addr = phys;
    node->next = vmm->table_list;
    vmm->table_list = node;
  }

  *parent_entry = turing_mmu_encode_pde(phys, aperture);
  return (NvMmuEntry *)virt;
}

/*  Insert a free VA range into the sorted free list & merging adjacent ranges
 *  start : first address
 *  size  : len in bytes
*/
static void insert_free_range(NvVmm *vmm, uint64_t start, uint64_t size) {
  if (size == 0) {
    return;
  }

  NvVaRangeNode *node = (NvVaRangeNode *)kmalloc(sizeof(NvVaRangeNode));
  if (!node) {
    return;
  }

  node->start = start;
  node->size = size;
  node->next = NULL;

  // Insert in ascending order by start address
  NvVaRangeNode **curr = &vmm->free_ranges;
  while (*curr && (*curr)->start < start) {
    curr = &(*curr)->next;
  }

  node->next = *curr;
  *curr = node;

  // Merge adjacent nodes
  NvVaRangeNode *it = vmm->free_ranges;
  while (it && it->next) {
    if (it->start + it->size == it->next->start) {
      NvVaRangeNode *merged = it->next;
      it->size += merged->size;
      it->next = merged->next;
      kfree(merged);
    } else {
      it = it->next;
    }
  }
}

int nv_vmm_create(NvVmm *vmm, uint64_t va_start, uint64_t va_limit) {
  if (!vmm) {
    return -1;
  }

  if (va_limit == 0) {
    va_limit = NV_VMM_DEFAULT_VA_LIMIT;
  }

  if (va_start >= va_limit) {
    return -1;
  }

  if (nv_dma_alloc(&vmm->pdb, NV_VMM_PAGE_SIZE) != 0) {
    return -1;
  }

  vmm->table_list = NULL;
  vmm->free_ranges = NULL;
  vmm->va_start = va_start;
  vmm->va_limit = va_limit;

  insert_free_range(vmm, va_start, va_limit - va_start);
  return 0;
}

void nv_vmm_destroy(NvVmm *vmm) {
  if (!vmm) {
    return;
  }

  // Free all PTs
  NvTablePage *curr_page = vmm->table_list;
  while (curr_page) {
    NvTablePage *next = curr_page->next;
    pmm_free_frame((void *)curr_page->phys_addr);
    kfree(curr_page);
    curr_page = next;
  }
  vmm->table_list = NULL;

  // Free all VA nodes
  NvVaRangeNode *curr_range = vmm->free_ranges;
  while (curr_range) {
    NvVaRangeNode *next = curr_range->next;
    kfree(curr_range);
    curr_range = next;
  }
  vmm->free_ranges = NULL;

  nv_dma_free(&vmm->pdb);
  vmm->va_start = 0;
  vmm->va_limit = 0;
}

int nv_vmm_alloc_va(NvVmm *vmm, size_t size, uint64_t align, uint64_t *out_va) {
  if (!vmm || size == 0 || !out_va) {
    return -1;
  }

  if (align < NV_VMM_PAGE_SIZE) {
    align = NV_VMM_PAGE_SIZE;
  }

  uint64_t aligned_size = (size + NV_VMM_PAGE_SIZE - 1) & ~(NV_VMM_PAGE_SIZE - 1);

  NvVaRangeNode **prev = &vmm->free_ranges;
  NvVaRangeNode *curr = vmm->free_ranges;

  while (curr) {
    uint64_t aligned_start = (curr->start + align - 1) & ~(align - 1);
    uint64_t end = curr->start + curr->size;

    if (aligned_start >= curr->start && (aligned_start + aligned_size) <= end) {
      uint64_t leading_gap = aligned_start - curr->start;
      uint64_t trailing_gap = end - (aligned_start + aligned_size);

      *out_va = aligned_start;

      // Split the free node if necessary
      if (leading_gap > 0 && trailing_gap > 0) {
        curr->size = leading_gap;
        NvVaRangeNode *trailing_node = (NvVaRangeNode *)kmalloc(sizeof(NvVaRangeNode));
        if (!trailing_node) {
          return -1;
        }
        trailing_node->start = aligned_start + aligned_size;
        trailing_node->size = trailing_gap;
        trailing_node->next = curr->next;
        curr->next = trailing_node;
      } else if (leading_gap > 0) {
        curr->size = leading_gap;
      } else if (trailing_gap > 0) {
        curr->start = aligned_start + aligned_size;
        curr->size = trailing_gap;
      } else {
        *prev = curr->next;
        kfree(curr);
      }
      return 0;
    }
    prev = &curr->next;
    curr = curr->next;
  }

  // No suitable hole found
  return -1;
}

void nv_vmm_free_va(NvVmm *vmm, uint64_t gpu_va, size_t size) {
  if (!vmm || gpu_va == 0 || size == 0) {
    return;
  }

  uint64_t aligned_size = (size + NV_VMM_PAGE_SIZE - 1) & ~(NV_VMM_PAGE_SIZE - 1);
  insert_free_range(vmm, gpu_va, aligned_size);
}

/*  Map a 4KB‑aligned GPU VA range to physical memory
 *  0 on success, -1 on failure
*/
int nv_vmm_map(NvVmm *vmm, uint64_t gpu_va, uint64_t phys_addr, size_t size, NvMmuAperture aperture, int read_only, uint8_t kind) {
  if (!vmm || !vmm->pdb.virt_addr || size == 0) {
    return -1;
  }

  if ((gpu_va & (NV_VMM_PAGE_SIZE - 1)) != 0 || (phys_addr & (NV_VMM_PAGE_SIZE - 1)) != 0) {
    return -1;
  }

  size_t page_count = (size + NV_VMM_PAGE_SIZE - 1) / NV_VMM_PAGE_SIZE;
  NvMmuEntry *pde4 = (NvMmuEntry *)vmm->pdb.virt_addr;

  for (size_t i = 0; i < page_count; i++) {
    uint64_t curr_va = gpu_va + (i * NV_VMM_PAGE_SIZE);
    uint64_t curr_phys = phys_addr + (i * NV_VMM_PAGE_SIZE);

    // 48bit VA
    size_t pde4_idx = (curr_va >> 47) & 0x3;
    size_t pde3_idx = (curr_va >> 38) & 0x1FF;
    size_t pde2_idx = (curr_va >> 29) & 0x1FF;
    size_t pde1_idx = (curr_va >> 21) & 0xFF;
    size_t pte_idx  = (curr_va >> 12) & 0x1FF;

    NvMmuEntry *pde3 = get_or_alloc_table(vmm, &pde4[pde4_idx], NV_MMU_APERTURE_SYS_MEM_COHERENT);
    if (!pde3) {
      return -1;
    }

    NvMmuEntry *pde2 = get_or_alloc_table(vmm, &pde3[pde3_idx], NV_MMU_APERTURE_SYS_MEM_COHERENT);
    if (!pde2) {
      return -1;
    }

    NvMmuEntry *pde1 = get_or_alloc_table(vmm, &pde2[pde2_idx], NV_MMU_APERTURE_SYS_MEM_COHERENT);
    if (!pde1) {
      return -1;
    }

    // +0: Large Page Table (64KB) PDE
    // +8: Small Page Table (4KB) PDE
    NvMmuEntry *pd0_lpt_entry = (NvMmuEntry *)((uint8_t *)pde1 + (pde1_idx * 16) + 0);
    NvMmuEntry *pd0_spt_entry = (NvMmuEntry *)((uint8_t *)pde1 + (pde1_idx * 16) + 8);

    pd0_lpt_entry->word0 = 0;
    pd0_lpt_entry->word1 = 0;

    NvMmuEntry *pt = get_or_alloc_table(vmm, pd0_spt_entry, NV_MMU_APERTURE_SYS_MEM_COHERENT);
    if (!pt) {
      return -1;
    }

    pt[pte_idx] = turing_mmu_encode_pte(curr_phys, aperture, read_only, kind);
  }

  nv_dma_wmb();
  return 0;
}

/*  Unmap 4KB pages from GPU VA space */
int nv_vmm_unmap(NvVmm *vmm, uint64_t gpu_va, size_t size) {
  if (!vmm || !vmm->pdb.virt_addr || size == 0) {
    return -1;
  }

  size_t page_count = (size + NV_VMM_PAGE_SIZE - 1) / NV_VMM_PAGE_SIZE;
  NvMmuEntry *pde4 = (NvMmuEntry *)vmm->pdb.virt_addr;

  for (size_t i = 0; i < page_count; i++) {
    uint64_t curr_va = gpu_va + (i * NV_VMM_PAGE_SIZE);

    size_t pde4_idx = (curr_va >> 47) & 0x3;
    size_t pde3_idx = (curr_va >> 38) & 0x1FF;
    size_t pde2_idx = (curr_va >> 29) & 0x1FF;
    size_t pde1_idx = (curr_va >> 21) & 0xFF; // 256 entries
    size_t pte_idx = (curr_va >> 12) & 0x1FF;

    uint64_t raw_pde4 = ((uint64_t)pde4[pde4_idx].word1 << 32) | pde4[pde4_idx].word0;
    if ((raw_pde4 & 0x6ULL) == 0) continue;
    uint64_t pde3_phys = (raw_pde4 & ~0xFFULL) << 4;

    NvMmuEntry *pde3 = (NvMmuEntry *)(pde3_phys + HHDM_BASE);
    uint64_t raw_pde3 = ((uint64_t)pde3[pde3_idx].word1 << 32) | pde3[pde3_idx].word0;

    if ((raw_pde3 & 0x6ULL) == 0) continue;
    uint64_t pde2_phys = (raw_pde3 & ~0xFFULL) << 4;

    NvMmuEntry *pde2 = (NvMmuEntry *)(pde2_phys + HHDM_BASE);
    uint64_t raw_pde2 = ((uint64_t)pde2[pde2_idx].word1 << 32) | pde2[pde2_idx].word0;

    if ((raw_pde2 & 0x6ULL) == 0) continue;
    uint64_t pde1_phys = (raw_pde2 & ~0xFFULL) << 4;

    NvMmuEntry *pde1 = (NvMmuEntry *)(pde1_phys + HHDM_BASE);
    NvMmuEntry *pd0_spt_entry = (NvMmuEntry *)((uint8_t *)pde1 + (pde1_idx * 16) + 8);

    uint64_t raw_pd0 = ((uint64_t)pd0_spt_entry->word1 << 32) | pd0_spt_entry->word0;
    if ((raw_pd0 & 0x6ULL) == 0) continue;
    uint64_t pt_phys = (raw_pd0 & ~0xFFULL) << 4;

    NvMmuEntry *pt = (NvMmuEntry *)(pt_phys + HHDM_BASE);

    pt[pte_idx].word0 = 0;
    pt[pte_idx].word1 = 0;

    if (is_table_empty(pt)) {
      pd0_spt_entry->word0 = 0;
      pd0_spt_entry->word1 = 0;
      remove_table_page(vmm, pt_phys);

      if (is_table_empty(pde1)) {
        pde2[pde2_idx].word0 = 0;
        pde2[pde2_idx].word1 = 0;
        remove_table_page(vmm, pde1_phys);

        if (is_table_empty(pde2)) {
          pde3[pde3_idx].word0 = 0;
          pde3[pde3_idx].word1 = 0;
          remove_table_page(vmm, pde2_phys);

          if (is_table_empty(pde3)) {
            pde4[pde4_idx].word0 = 0;
            pde4[pde4_idx].word1 = 0;
            remove_table_page(vmm, pde3_phys);
          }
        }
      }
    }
  }

  nv_dma_wmb();
  return 0;
}
