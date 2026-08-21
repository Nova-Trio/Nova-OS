#include "acpi.h"
#include <vmm.h>
#include <pmm.h>
#include <heap.h>
#include <con.h>

typedef struct {
  uint64_t phys_addr;
  char signature[4];
} AcpiTableEntry;

static AcpiTableEntry *g_tables = NULL;
static size_t g_table_count = 0;
static uint8_t g_acpi_rev = 0;

static inline void outb(uint16_t port, uint8_t val) {
  __asm__ volatile("outb %0, %1" : : "a"(val), "Nd"(port));
}

static inline void outw(uint16_t port, uint16_t val) {
  __asm__ volatile("outw %0, %1" : : "a"(val), "Nd"(port));
}

static inline void outl(uint16_t port, uint32_t val) {
  __asm__ volatile("outl %0, %1" : : "a"(val), "Nd"(port));
}

static int acpi_verify_checksum(const void *addr, size_t length) {
  const uint8_t *bytes = (const uint8_t *)addr;
  uint8_t sum = 0;
  for (size_t i = 0; i < length; i++) {
    sum += bytes[i];
  }
  return sum == 0;
}

static void *acpi_map_table(uint64_t addr) {
  if (!addr) {
    return NULL;
  }
  if (addr >= HHDM_BASE) {
    return (void *)addr;
  }
  return (void *)(addr + HHDM_BASE);
}

void acpi_init(void *rsdp_phys) {
  if (!rsdp_phys) {
    kprintf("[ACPI] Error: NULL RSDP address\n");
    return;
  }

  AcpiRsdp *rsdp = (AcpiRsdp *)acpi_map_table((uint64_t)rsdp_phys);
  if (!rsdp) {
    kprintf("[ACPI] Error: Failed to map RSDP\n");
    return;
  }

  for (size_t i = 0; i < 8; i++) {
    if (rsdp->signature[i] != "RSD PTR "[i]) {
      kprintf("[ACPI] Error: Invalid RSDP signature\n");
      return;
    }
  }

  if (!acpi_verify_checksum(rsdp, 20)) {
    kprintf("[ACPI] Error: RSDP base checksum failed\n");
    return;
  }

  g_acpi_rev = rsdp->revision;
  if (g_acpi_rev < 2 || rsdp->xsdt_address == 0) {
    kprintf("[ACPI] Error: ACPI 2.0+ (XSDT) required for UEFI\n");
    return;
  }

  if (!acpi_verify_checksum(rsdp, rsdp->length)) {
    kprintf("[ACPI] Error: RSDP extended checksum failed\n");
    return;
  }

  AcpiXsdt *xsdt = (AcpiXsdt *)acpi_map_table(rsdp->xsdt_address);
  if (!xsdt) {
    kprintf("[ACPI] Error: Failed to map XSDT\n");
    return;
  }

  if (!acpi_verify_checksum(xsdt, xsdt->header.length)) {
    kprintf("[ACPI] Error: XSDT checksum failed\n");
    return;
  }

  size_t entry_bytes = xsdt->header.length - sizeof(AcpiSdtHeader);
  g_table_count = entry_bytes / sizeof(uint64_t);

  if (g_table_count == 0) {
    kprintf("[ACPI] Warning: No tables found in XSDT\n");
    return;
  }

  g_tables = (AcpiTableEntry *)kmalloc(g_table_count * sizeof(AcpiTableEntry));
  if (!g_tables) {
    kprintf("[ACPI] Error: Failed to allocate table cache\n");
    return;
  }

  size_t valid_tables = 0;
  for (size_t i = 0; i < g_table_count; i++) {
    uint64_t table_phys = xsdt->tables[i];
    if (!table_phys) {
      continue;
    }

    AcpiSdtHeader *hdr = (AcpiSdtHeader *)acpi_map_table(table_phys);
    if (!hdr) {
      continue;
    }

    if (!acpi_verify_checksum(hdr, hdr->length)) {
      continue;
    }

    g_tables[valid_tables].phys_addr = table_phys;
    for (size_t c = 0; c < 4; c++) {
      g_tables[valid_tables].signature[c] = hdr->signature[c];
    }
    valid_tables++;
  }

  g_table_count = valid_tables;
  kprintf("[ACPI] Initialized: %u valid tables indexed (ACPI rev %u)\n", (uint32_t)g_table_count, (uint32_t)g_acpi_rev);
}

AcpiSdtHeader *acpi_find_table(const char *signature, size_t index) {
  if (!signature || !g_tables) {
    return NULL;
  }

  size_t match_count = 0;
  for (size_t i = 0; i < g_table_count; i++) {
    int matches = 1;
    for (size_t c = 0; c < 4; c++) {
      if (g_tables[i].signature[c] != signature[c]) {
        matches = 0;
        break;
      }
    }

    if (matches) {
      if (match_count == index) {
        return (AcpiSdtHeader *)acpi_map_table(g_tables[i].phys_addr);
      }
      match_count++;
    }
  }

  return NULL;
}

void acpi_dump_tables(void) {
  for (size_t i = 0; i < g_table_count; i++) {
    AcpiSdtHeader *hdr = (AcpiSdtHeader *)acpi_map_table(g_tables[i].phys_addr);
    if (!hdr) continue;

    kprintf("[%u] %c%c%c%c at %p (length %u bytes)\n",
            (uint32_t)i,
            hdr->signature[0], hdr->signature[1], hdr->signature[2], hdr->signature[3],
            hdr, hdr->length
    );
  }
}


__attribute__((noreturn)) void acpi_reboot(void) {
  AcpiFadt *fadt = (AcpiFadt *)acpi_find_table("FACP", 0);

  if (fadt && (fadt->flags & (1 << 10))) {
    AcpiGas *reg = &fadt->reset_reg;
    uint8_t val = fadt->reset_value;

    if (reg->address_space == 1) {
      if (reg->bit_width == 16) {
        outw((uint16_t)reg->address, val);
      } else if (reg->bit_width == 32) {
        outl((uint16_t)reg->address, val);
      } else {
        outb((uint16_t)reg->address, val);
      }
    } else if (reg->address_space == 0 && reg->address != 0) {
      uint64_t virt = reg->address + HHDM_BASE;
      PageDirectory kernel_pml4 = vmm_get_kernel_pml4();
      if (!vmm_virt_to_phys(kernel_pml4, virt)) {
        vmm_map_page(kernel_pml4, virt, reg->address, VMM_FLAG_PRESENT | VMM_FLAG_WRITABLE | VMM_FLAG_NO_CACHE);
      }

      if (reg->bit_width == 16) {
        *(volatile uint16_t *)virt = val;
      } else if (reg->bit_width == 32) {
        *(volatile uint32_t *)virt = val;
      } else {
        *(volatile uint8_t *)virt = val;
      }
    }
  }

  outb(0xCF9, 0x02);
  outb(0xCF9, 0x06);

  struct {
    uint16_t limit;
    uint64_t base;
  } __attribute__((packed)) null_idtr = { 0, 0 };

  __asm__ volatile("lidt %0; int3" : : "m"(null_idtr));

  for (;;) {
    __asm__ volatile("cli; hlt");
  }
}
