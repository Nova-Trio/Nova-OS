#include <nv_mmu.h>

#define TU102_PTE_VALID_TRUE 0x1U
#define TU102_PTE_READ_ONLY_TRUE 0x1U
#define TU102_PTE_ADDRESS_SHIFT 12U

#define TU102_PDE_SIZE_FULL 0x0U
#define TU102_PDE_ADDRESS_SHIFT 12U
#define TU102_PDE_VALID_TRUE  0x1U

NvPte turing_mmu_encode_pte(uint64_t target_phys, NvMmuAperture aperture, int read_only, uint8_t kind) {
  NvPte pte;

  uint32_t frame_idx = (uint32_t)(target_phys >> TU102_PTE_ADDRESS_SHIFT);

  pte.word0 = (frame_idx << 4) | TU102_PTE_VALID_TRUE;
  if (read_only) {
    pte.word0 |= (TU102_PTE_READ_ONLY_TRUE << 2);
  }

  pte.word1 = ((uint32_t)(aperture & 0x3) << 1) | ((uint32_t)kind << 4);

  return pte;
}

NvPde turing_mmu_encode_pde(uint64_t pt_phys, NvMmuAperture aperture) {
  NvPde pde;

  uint32_t frame_idx = (uint32_t)(pt_phys >> TU102_PDE_ADDRESS_SHIFT);

  pde.word0 = (frame_idx << 4) | (TU102_PDE_SIZE_FULL << 2) | TU102_PDE_VALID_TRUE;
  pde.word1 = ((uint32_t)aperture & 0x3) << 1;

  return pde;
}
