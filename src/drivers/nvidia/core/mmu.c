#include <nv_mmu.h>
#include <nv_device.h>

#define TU102_PTE_VALID_TRUE 0x1U
#define TU102_PTE_READ_ONLY_TRUE 0x1U
#define TU102_PTE_ADDRESS_SHIFT 12U

#define TU102_PDE_SIZE_FULL 0x0U
#define TU102_PDE_ADDRESS_SHIFT 12U
#define TU102_PDE_VALID_TRUE 0x1U

#define TU102_MMU_VALID BIT_ULL(0)
#define TU102_MMU_VOL BIT_ULL(3)
#define TU102_MMU_RO BIT_ULL(6)

static const char *tu102_fault_reason_name(uint8_t reason) {
  switch (reason) {
    case 0x00: return "PDE (PDE invalid / not present)";
    case 0x01: return "PDE_SIZE";
    case 0x02: return "PTE (PTE invalid / not present)";
    case 0x03: return "VA_LIMIT_VIOLATION";
    case 0x04: return "UNBOUND_INST_BLOCK";
    case 0x05: return "PRIV_VIOLATION";
    case 0x06: return "RO_VIOLATION";
    case 0x07: return "WO_VIOLATION";
    case 0x08: return "PITCH_MASK_VIOLATION";
    case 0x09: return "WORK_CREATION";
    case 0x0A: return "UNSUPPORTED_APERTURE";
    case 0x0B: return "COHERENT_SYS_CACHE_MISS";
    case 0x0C: return "BAD_PDE";
    case 0x0D: return "BAD_PTE";
    case 0x0E: return "ZERO_FB";
    case 0x0F: return "DMA_MAP";
    default: return "UNKNOWN";
  }
}

void nv_mmu_dump_fault(const NvDevice *dev) {
  if (!dev || !dev->bar0.virt_addr) {
    return;
  }

  const uint32_t addrlo = nv_rd32(dev, NV_PFB_PRI_MMU_FAULT_ADDR_LO);
  const uint32_t addrhi = nv_rd32(dev, NV_PFB_PRI_MMU_FAULT_ADDR_HI);
  const uint32_t info0 = nv_rd32(dev, NV_PFB_PRI_MMU_FAULT_INST_LO);
  const uint32_t insthi = nv_rd32(dev, NV_PFB_PRI_MMU_FAULT_INST_HI);
  const uint32_t info1 = nv_rd32(dev, NV_PFB_PRI_MMU_FAULT_INFO);

  uint64_t fault_addr = ((uint64_t)addrhi << 32) | addrlo;
  uint64_t inst_addr = ((uint64_t)insthi << 32) | (info0 & 0xFFFFF000U);
  uint8_t engine_id = info0 & 0xFF;
  uint8_t valid = (info1 & 0x80000000U) >> 31;
  uint8_t gpc_id = (info1 & 0x1F000000U) >> 24;
  uint8_t is_hub = (info1 & 0x00100000U) >> 20;
  uint8_t access_type = (info1 & 0x000F0000U) >> 16;
  uint8_t client_id = (info1 & 0x00007F00U) >> 8;
  uint8_t reason = (info1 & 0x0000001FU);

  kprintf("[NV/TU] ***HARDWARE FAULT***\n");
  kprintf("[NV/TU] Valid: %u | Reason: 0x%02x (%s)\n", valid, reason, tu102_fault_reason_name(reason));
  kprintf("[NV/TU] Fault Address : 0x%016llx\n", fault_addr);
  kprintf("[NV/TU] Inst (PDB ptr): 0x%016llx\n", inst_addr);
  kprintf("[NV/TU] Access: 0x%02x | Location: %s (GPC %u, Client 0x%02x, Engine 0x%02x)\n",
          access_type, is_hub ? "HUB (BAR/Host)" : "GPC", gpc_id, client_id, engine_id);
  kprintf("[NV/TU] ***END***");

  // ACK the fault
  nv_wr32(dev, NV_PFB_PRI_MMU_FAULT_ACK, 0x80000000U);
}


NvPte turing_mmu_encode_pte(uint64_t target_phys, NvMmuAperture aperture, int read_only, uint8_t kind) {
  uint64_t entry = (target_phys >> 4) | TU102_MMU_VALID;

  entry |= ((uint64_t)(aperture & 0x3) << 1);

  if (aperture == NV_MMU_APERTURE_SYS_MEM_COHERENT || aperture == NV_MMU_APERTURE_SYS_MEM_NONCOHERENT) {
    entry |= TU102_MMU_VOL;
  }

  if (read_only) {
    entry |= TU102_MMU_RO;
  }

  entry |= ((uint64_t)kind << 56);

  NvPte pte;
  pte.word0 = (uint32_t)(entry & 0xFFFFFFFFU);
  pte.word1 = (uint32_t)(entry >> 32);
  return pte;
}

NvPde turing_mmu_encode_pde(uint64_t pt_phys, NvMmuAperture aperture) {
  uint64_t entry = (pt_phys >> 4) | ((uint64_t)(aperture & 0x3) << 1);

  if (aperture == NV_MMU_APERTURE_SYS_MEM_COHERENT || aperture == NV_MMU_APERTURE_SYS_MEM_NONCOHERENT) {
    entry |= TU102_MMU_VOL;
  }

  NvPde pde;
  pde.word0 = (uint32_t)(entry & 0xFFFFFFFFU);
  pde.word1 = (uint32_t)(entry >> 32);
  return pde;
}
