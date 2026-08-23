#pragma once
#include <stdint.h>
#include <stddef.h>

typedef enum {
  NV_MMU_APERTURE_VID_MEM = 0x0, // local VRAM
  NV_MMU_APERTURE_PEER_MEM = 0x1, // Only MMU can target peer mem, this can be another PCIe device eg another GPU VRAM
  NV_MMU_APERTURE_SYS_MEM_COHERENT = 0x2, // system RAM (cache coherency)
  NV_MMU_APERTURE_SYS_MEM_NONCOHERENT = 0x3, // system RAM
} NvMmuAperture;

#ifndef BIT_ULL
#define BIT_ULL(nr) (1ULL << (nr))
#endif

#define NV_PFB_PRI_MMU_INVALIDATE_PDB_LO 0x00B830A0
#define NV_PFB_PRI_MMU_INVALIDATE_PDB_HI 0x00B830A4
#define NV_PFB_PRI_MMU_INVALIDATE_CMD 0x00B830B0
#define NV_PFB_PRI_MMU_INVALIDATE_TRIGGER 0x80000000U
#define NV_PFB_PRI_MMU_INVALIDATE_ALL 0x00000007U

#define NV_PFB_PRI_MMU_FAULT_ADDR_LO 0x00B83080
#define NV_PFB_PRI_MMU_FAULT_ADDR_HI 0x00B83084
#define NV_PFB_PRI_MMU_FAULT_INST_LO 0x00B83088
#define NV_PFB_PRI_MMU_FAULT_INST_HI 0x00B8308C
#define NV_PFB_PRI_MMU_FAULT_INFO 0x00B83090
#define NV_PFB_PRI_MMU_FAULT_ACK 0x00B83094


// This goes into the PT/PD
typedef struct {
  uint32_t word0;
  uint32_t word1;
} __attribute__((packed)) NvMmuEntry;

typedef NvMmuEntry NvPte;
typedef NvMmuEntry NvPde;

// This maps a GPU virtual page to a physical page.
/*
 * target_phys : Phys address of the page. Must be page aligned
 * aperture    : Where is the target
 * read_only   : self explanatory (0 = R/W)
 * kind        : some compression as i understood? (TODO)
 *
 * ret         : populated PTE
*/
NvPte turing_mmu_encode_pte(uint64_t target_phys, NvMmuAperture aperture, int read_only, uint8_t kind);

/*
 * pt_phys    : Phys address of the page table itself (must be aligned)
 * aperture   : Where is the target
 *
 * ret        : populated PDE
*/
NvPde turing_mmu_encode_pde(uint64_t pt_phys, NvMmuAperture aperture);

struct NvDevice;
void nv_mmu_tlb_invalidate(const struct NvDevice *dev, uint64_t pdb_phys);

void nv_mmu_dump_fault(const struct NvDevice *dev);
