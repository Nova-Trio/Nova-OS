#pragma once
#include <nv_dma.h>
#include <nv_mmu.h>

// Some gpus support larger pages
#define NV_VMM_PAGE_SIZE 4096ULL

#define NV_VMM_DEFAULT_VA_START 0x10000ULL // 256MB, can be lower, 0x0 should be unmapped
#define NV_VMM_DEFAULT_VA_LIMIT 0x1FFFFFFFFFFFULL // 2^49-1 (49-bit VA)

// Single page table page tracked by the VMM
typedef struct NvTablePage {
  uint64_t phys_addr; // used in PDEs
  struct NvTablePage *next; // linked list for cleanup
} NvTablePage;

// Free VA range node
typedef struct NvVaRangeNode {
  uint64_t start; // first address of the range
  uint64_t size; // size in bytes (page aligned)
  struct NvVaRangeNode *next;
} NvVaRangeNode;

// VMM context
typedef struct {
  NvDmaBuffer pdb; // Root of PML4 table
  NvTablePage *table_list; // all PTs allocated by this VMM
  NvVaRangeNode *free_ranges; // list of free GPU VA ranges
  uint64_t va_start; // first usable VA
  uint64_t va_limit; // last usable VA
} NvVmm;

/*  Allocates the root PML4 (4K zeroed) & inits the free list with [VA_START, VA_LIMIT] range
 *  0 success, -1 failure
*/
int nv_vmm_create(NvVmm *vmm, uint64_t va_start, uint64_t va_limit);

/*  Destroy a VMM
 *
 *  Ensure the GPU isnt using the address space before destroying
*/
void nv_vmm_destroy(NvVmm *vmm);

/*  Allocate a GPU virtual address
 *  if out of space, returns -1
 *
 *  size  : requested bytes (rounded to NV_VMM_PAGE_SIZE)
 *  align : required alignment (must be pwr of 2, >= NV_VMM_PAGE_SIZE)
 *  out_va: filled with allocated VA on success (0)
*/
int nv_vmm_alloc_va(NvVmm *vmm, size_t size, uint64_t align, uint64_t *out_va);

/*  Free a previously allocated VA range */
void nv_vmm_free_va(NvVmm *vmm, uint64_t gpu_va, size_t size);

/*  Map physical memory into the GPU address space
 *
 *  gpu_va    : must be page aligned and previously allocated
 *  phys_addr : must be page aligned
 *  size      : bytes
 *  aperture  : where the memory lives
 *  read_only : self explanatory
 *  kind      : type
 *
 *  Returns 0 on success, -1 if table allocation fails.
*/
int nv_vmm_map(NvVmm *vmm, uint64_t gpu_va, uint64_t phys_addr, size_t size, NvMmuAperture aperture, int read_only, uint8_t kind);

/*  Unmap a VA range
 *  gpu_va : page-aligned
 *  size   : bytes (rounded up)
 *  0 on success, -1 fail
*/
int nv_vmm_unmap(NvVmm *vmm, uint64_t gpu_va, size_t size);
