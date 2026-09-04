#pragma once
#include <stdint.h>
#include <stddef.h>
#include <bootinfo.h>

#define PAGE_SIZE 4096ULL

#define VMM_FLAG_PRESENT (1ULL << 0)
#define VMM_FLAG_WRITABLE (1ULL << 1)
#define VMM_FLAG_USER (1ULL << 2)
#define VMM_FLAG_WRITE_THROUGH (1ULL << 3)
#define VMM_FLAG_NO_CACHE (1ULL << 4)
#define VMM_FLAG_HUGE (1ULL << 7)
#define VMM_FLAG_NO_EXECUTE (1ULL << 63)

typedef uint64_t *PageDirectory;

void vmm_init(BootInfo *boot_info);

int vmm_map_page(PageDirectory pml4, uint64_t virt, uint64_t phys, uint64_t flags);
int vmm_unmap_page(PageDirectory pml4, uint64_t virt);
int vmm_map_range(PageDirectory pml4, uint64_t virt_start, uint64_t phys_start, uint64_t size, uint64_t flags);
int vmm_unmap_range(PageDirectory pml4, uint64_t virt_start, uint64_t size);

uint64_t vmm_virt_to_phys(PageDirectory pml4, uint64_t virt);

PageDirectory vmm_get_kernel_pml4(void);
void vmm_switch_pml4(PageDirectory pml4);

PageDirectory vmmCreateAddressSpace(void);
void vmmDestroyAddressSpace(PageDirectory pml4);

static inline void vmm_invlpg(uint64_t virt) {
  __asm__ volatile("invlpg (%0)" : : "r"(virt) : "memory");
}
