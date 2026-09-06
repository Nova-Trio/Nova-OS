#pragma once
#include <stdint.h>
#include <stddef.h>
#include <bootinfo.h>
#include <idt.h>

#define PAGE_SIZE 4096ULL

#define USER_SPACE_MAX 0x0000800000000000ULL
#define USER_STACK_MAX_SIZE (8ULL * 1024 * 1024)

#define VMM_FLAG_PRESENT (1ULL << 0)
#define VMM_FLAG_WRITABLE (1ULL << 1)
#define VMM_FLAG_USER (1ULL << 2)
#define VMM_FLAG_WRITE_THROUGH (1ULL << 3)
#define VMM_FLAG_NO_CACHE (1ULL << 4)
#define VMM_FLAG_HUGE (1ULL << 7)
#define VMM_FLAG_NO_EXECUTE (1ULL << 63)

#define VMA_READ (1U << 0)
#define VMA_WRITE (1U << 1)
#define VMA_EXEC (1U << 2)
#define VMA_USER (1U << 3)
#define VMA_ANON (1U << 4)
#define VMA_STACK (1U << 5)

typedef struct Vma {
  uint64_t start;
  uint64_t end;
  uint32_t flags;
  struct Vma *next;
  struct Vma *prev;
} Vma;

struct Process;

typedef uint64_t *PageDirectory;

void vmmInit(BootInfo *bootInfo);

int vmmMapPage(PageDirectory pml4, uint64_t virt, uint64_t phys, uint64_t flags);
int vmmUnmapPage(PageDirectory pml4, uint64_t virt);
int vmmMapRange(PageDirectory pml4, uint64_t virtStart, uint64_t physStart, uint64_t size, uint64_t flags);
int vmmUnmapRange(PageDirectory pml4, uint64_t virtStart, uint64_t size);

uint64_t vmmVirtToPhys(PageDirectory pml4, uint64_t virt);

PageDirectory vmmGetKernelPml4(void);
void vmmSwitchPml4(PageDirectory pml4);

PageDirectory vmmCreateAddressSpace(void);
void vmmDestroyAddressSpace(PageDirectory pml4);

static inline void vmmInvlpg(uint64_t virt) {
  __asm__ volatile("invlpg (%0)" : : "r"(virt) : "memory");
}

Vma *vmaCreate(struct Process *proc, uint64_t start, uint64_t size, uint32_t flags);
int vmaDestroy(struct Process *proc, uint64_t start, uint64_t size);
Vma *vmaFind(struct Process *proc, uint64_t addr);
void vmaDestroyAll(struct Process *proc);

int validateUserRange(const void *userPtr, size_t size, int write);
int copyFromUser(void *dst, const void *src, size_t n);
int copyToUser(void *dst, const void *src, size_t n);

void vmmPageFaultHandler(Registers *regs);

#define vmm_init vmmInit
#define vmm_map_page vmmMapPage
#define vmm_unmap_page vmmUnmapPage
#define vmm_map_range vmmMapRange
#define vmm_unmap_range vmmUnmapRange
#define vmm_virt_to_phys vmmVirtToPhys
#define vmm_get_kernel_pml4 vmmGetKernelPml4
#define vmm_switch_pml4 vmmSwitchPml4
#define vmm_invlpg vmmInvlpg
#define vma_create vmaCreate
#define vma_destroy vmaDestroy
#define vma_find vmaFind
#define vma_destroy_all vmaDestroyAll
#define validate_user_range validateUserRange
#define copy_from_user copyFromUser
#define copy_to_user copyToUser
#define vmm_page_fault_handler vmmPageFaultHandler
