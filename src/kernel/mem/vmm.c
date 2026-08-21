#include "vmm.h"
#include "pmm.h"

#define PTE_ADDR_MASK 0x000FFFFFFFFFF000ULL

static PageDirectory g_kernel_pml4 = NULL;

static uint64_t *get_or_alloc_table(uint64_t *entry, uint64_t flags) {
  if (*entry & VMM_FLAG_PRESENT) {
    if (flags & VMM_FLAG_USER) {
      *entry |= VMM_FLAG_USER;
    }
    if (flags & VMM_FLAG_WRITABLE) {
      *entry |= VMM_FLAG_WRITABLE;
    }
    return (uint64_t *)((*entry & PTE_ADDR_MASK) + HHDM_BASE);
  }

  void *frame = pmm_alloc_frame();
  if (!frame) {
    return NULL;
  }

  uint64_t *table = (uint64_t *)((uint64_t)frame + HHDM_BASE);
  for (size_t i = 0; i < 512; i++) {
    table[i] = 0;
  }

  uint64_t entry_flags = VMM_FLAG_PRESENT | VMM_FLAG_WRITABLE;
  if (flags & VMM_FLAG_USER) {
    entry_flags |= VMM_FLAG_USER;
  }

  *entry = (uint64_t)frame | entry_flags;
  return table;
}

void vmm_init(BootInfo *boot_info) {
  void *new_pml4_phys = pmm_alloc_frame();
  if (!new_pml4_phys) {
    return;
  }

  g_kernel_pml4 = (PageDirectory)((uint64_t)new_pml4_phys + HHDM_BASE);

  for (size_t i = 0; i < 512; i++) {
    g_kernel_pml4[i] = 0;
  }

  for (size_t i = 256; i < 512; i++) {
    g_kernel_pml4[i] = boot_info->pml4[i];
  }

  vmm_switch_pml4(g_kernel_pml4);
  boot_info->pml4 = g_kernel_pml4;
}

int vmm_map_page(PageDirectory pml4, uint64_t virt, uint64_t phys, uint64_t flags) {
  virt &= ~(PAGE_SIZE - 1);
  phys &= ~(PAGE_SIZE - 1);

  size_t pml4_idx = (virt >> 39) & 0x1FF;
  size_t pdpt_idx = (virt >> 30) & 0x1FF;
  size_t pd_idx = (virt >> 21) & 0x1FF;
  size_t pt_idx = (virt >> 12) & 0x1FF;

  uint64_t *pdpt = get_or_alloc_table(&pml4[pml4_idx], flags);
  if (!pdpt) {
    return -1;
  }

  uint64_t *pd = get_or_alloc_table(&pdpt[pdpt_idx], flags);
  if (!pd) {
    return -1;
  }

  uint64_t *pt = get_or_alloc_table(&pd[pd_idx], flags);
  if (!pt) {
    return -1;
  }

  pt[pt_idx] = (phys & PTE_ADDR_MASK) | flags | VMM_FLAG_PRESENT;
  vmm_invlpg(virt);
  return 0;
}

int vmm_unmap_page(PageDirectory pml4, uint64_t virt) {
  virt &= ~(PAGE_SIZE - 1);

  size_t pml4_idx = (virt >> 39) & 0x1FF;
  size_t pdpt_idx = (virt >> 30) & 0x1FF;
  size_t pd_idx = (virt >> 21) & 0x1FF;
  size_t pt_idx = (virt >> 12) & 0x1FF;

  if (!(pml4[pml4_idx] & VMM_FLAG_PRESENT)) {
    return 0;
  }

  uint64_t *pdpt = (uint64_t *)((pml4[pml4_idx] & PTE_ADDR_MASK) + HHDM_BASE);
  if (!(pdpt[pdpt_idx] & VMM_FLAG_PRESENT)) {
    return 0;
  }

  uint64_t *pd = (uint64_t *)((pdpt[pdpt_idx] & PTE_ADDR_MASK) + HHDM_BASE);
  if (!(pd[pd_idx] & VMM_FLAG_PRESENT)) {
    return 0;
  }

  if (pd[pd_idx] & VMM_FLAG_HUGE) {
    pd[pd_idx] = 0;
    vmm_invlpg(virt);
    return 0;
  }

  uint64_t *pt = (uint64_t *)((pd[pd_idx] & PTE_ADDR_MASK) + HHDM_BASE);
  pt[pt_idx] = 0;
  vmm_invlpg(virt);
  return 0;
}

int vmm_map_range(PageDirectory pml4, uint64_t virt_start, uint64_t phys_start, uint64_t size, uint64_t flags) {
  uint64_t aligned_size = (size + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1);

  for (uint64_t offset = 0; offset < aligned_size; offset += PAGE_SIZE) {
    if (vmm_map_page(pml4, virt_start + offset, phys_start + offset, flags) != 0) {
      return -1;
    }
  }

  return 0;
}

int vmm_unmap_range(PageDirectory pml4, uint64_t virt_start, uint64_t size) {
  uint64_t aligned_size = (size + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1);

  for (uint64_t offset = 0; offset < aligned_size; offset += PAGE_SIZE) {
    vmm_unmap_page(pml4, virt_start + offset);
  }

  return 0;
}

uint64_t vmm_virt_to_phys(PageDirectory pml4, uint64_t virt) {
  size_t pml4_idx = (virt >> 39) & 0x1FF;
  size_t pdpt_idx = (virt >> 30) & 0x1FF;
  size_t pd_idx = (virt >> 21) & 0x1FF;
  size_t pt_idx = (virt >> 12) & 0x1FF;
  uint64_t offset = virt & (PAGE_SIZE - 1);

  if (!(pml4[pml4_idx] & VMM_FLAG_PRESENT)) {
    return 0;
  }

  uint64_t *pdpt = (uint64_t *)((pml4[pml4_idx] & PTE_ADDR_MASK) + HHDM_BASE);
  if (!(pdpt[pdpt_idx] & VMM_FLAG_PRESENT)) {
    return 0;
  }

  if (pdpt[pdpt_idx] & VMM_FLAG_HUGE) {
    return (pdpt[pdpt_idx] & 0x000FFFFFC0000000ULL) | (virt & 0x3FFFFFFFULL);
  }

  uint64_t *pd = (uint64_t *)((pdpt[pdpt_idx] & PTE_ADDR_MASK) + HHDM_BASE);
  if (!(pd[pd_idx] & VMM_FLAG_PRESENT)) {
    return 0;
  }

  if (pd[pd_idx] & VMM_FLAG_HUGE) {
    return (pd[pd_idx] & 0x000FFFFFFFE00000ULL) | (virt & 0x1FFFFFULL);
  }

  uint64_t *pt = (uint64_t *)((pd[pd_idx] & PTE_ADDR_MASK) + HHDM_BASE);
  if (!(pt[pt_idx] & VMM_FLAG_PRESENT)) {
    return 0;
  }

  return (pt[pt_idx] & PTE_ADDR_MASK) | offset;
}

PageDirectory vmm_get_kernel_pml4(void) {
  return g_kernel_pml4;
}

void vmm_switch_pml4(PageDirectory pml4) {
  uint64_t pml4_phys = (uint64_t)pml4 - HHDM_BASE;
  __asm__ volatile("mov %0, %%cr3" : : "r"(pml4_phys) : "memory");
}
