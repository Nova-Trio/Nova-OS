#include "krsym.h"
#include <string.h>
#include <console.h>
#include <heap.h>
#include <vmm.h>
#include <pmm.h>
#include <idt.h>
#include <lapic.h>
#include <hpet.h>
#include <pcie.h>
#include <fat32.h>

extern const KernelSymbol __start_kernel_syms[];
extern const KernelSymbol __stop_kernel_syms[];

static size_t g_ksym_count = 0;

void krsym_init(void) {
  g_ksym_count = (size_t)(__stop_kernel_syms - __start_kernel_syms);
}

uint64_t krsym_lookup(const char *name) {
  if (!name) {
    return 0;
  }

  for (size_t i = 0; i < g_ksym_count; i++) {
    if (strcmp(__start_kernel_syms[i].name, name) == 0) {
      return __start_kernel_syms[i].addr;
    }
  }

  return 0;
}

EXPORT_SYMBOL(kprintf);
EXPORT_SYMBOL(kmalloc);
EXPORT_SYMBOL(kzalloc);
EXPORT_SYMBOL(krealloc);
EXPORT_SYMBOL(kfree);
EXPORT_SYMBOL(vmm_map_page);
EXPORT_SYMBOL(vmm_unmap_page);
EXPORT_SYMBOL(vmm_get_kernel_pml4);
EXPORT_SYMBOL(pmm_alloc_frame);
EXPORT_SYMBOL(pmm_free_frame);
EXPORT_SYMBOL(hpet_sleep_ms);
EXPORT_SYMBOL(hpet_sleep_us);
EXPORT_SYMBOL(hpet_get_nanos);
EXPORT_SYMBOL(hpet_get_millis);
EXPORT_SYMBOL(lapic_eoi);
EXPORT_SYMBOL(idt_register_handler);
EXPORT_SYMBOL(idt_unregister_handler);
EXPORT_SYMBOL(pcie_find_device);
EXPORT_SYMBOL(pcie_find_class);
EXPORT_SYMBOL(pcie_get_device);
EXPORT_SYMBOL(pcie_get_device_count);
EXPORT_SYMBOL(pcie_read8);
EXPORT_SYMBOL(pcie_read16);
EXPORT_SYMBOL(pcie_read32);
EXPORT_SYMBOL(pcie_write8);
EXPORT_SYMBOL(pcie_write16);
EXPORT_SYMBOL(pcie_write32);
EXPORT_SYMBOL(pcie_find_capability);
EXPORT_SYMBOL(pcie_find_extended_capability);
EXPORT_SYMBOL(pcie_enable_bus_master);
EXPORT_SYMBOL(pcie_enable_msi);
EXPORT_SYMBOL(pcie_disable_msi);
EXPORT_SYMBOL(pcie_get_msix_table_size);
EXPORT_SYMBOL(pcie_enable_msix_vector);
EXPORT_SYMBOL(pcie_mask_msix_vector);
EXPORT_SYMBOL(pcie_enable_msix);
EXPORT_SYMBOL(pcie_disable_msix);
EXPORT_SYMBOL(memcpy);
EXPORT_SYMBOL(memset);
EXPORT_SYMBOL(memmove);
EXPORT_SYMBOL(memcmp);
EXPORT_SYMBOL(strcmp);
EXPORT_SYMBOL(strlen);
EXPORT_SYMBOL(vmm_map_range);
EXPORT_SYMBOL(vmm_unmap_range);
EXPORT_SYMBOL(vmm_virt_to_phys);
EXPORT_SYMBOL(pmm_alloc_frames);
EXPORT_SYMBOL(pmm_free_frames);
EXPORT_SYMBOL(fs_read_file);
EXPORT_SYMBOL(fs_stat);
EXPORT_SYMBOL(lapic_get_id);
EXPORT_SYMBOL(fs_read);
EXPORT_SYMBOL(fs_list_dir);
EXPORT_SYMBOL(pmm_alloc_aligned_frames);
