#include "hpet.h"
#include "acpi.h"
#include "vmm.h"
#include "console.h"

#define HPET_REG_CAPABILITIES 0x000
#define HPET_REG_CONFIGURATION 0x010
#define HPET_REG_STATUS 0x020
#define HPET_REG_MAIN_COUNTER 0x0F0

#define HPET_CFG_ENABLE (1ULL << 0)
#define HPET_CFG_LEGACY_REPLACE (1ULL << 1)

#define HPET_CAP_64BIT (1ULL << 13)

#define FEMTOSECONDS_PER_SECOND 1000000000000000ULL
#define FEMTOSECONDS_PER_MICRO 1000000000ULL
#define FEMTOSECONDS_PER_MILLI 1000000000000ULL
#define FEMTOSECONDS_PER_NANO 1000000ULL

static volatile uint8_t *g_hpet_base = NULL;
static uint32_t g_period_fs = 0;
static uint64_t g_frequency_hz = 0;
static int g_is_64bit = 0;

static inline uint64_t hpet_read_reg(uint32_t offset) {
  return *(volatile uint64_t *)(g_hpet_base + offset);
}

static inline void hpet_write_reg(uint32_t offset, uint64_t value) {
  *(volatile uint64_t *)(g_hpet_base + offset) = value;
}

void hpet_init(void) {
  AcpiHpetTable *hpet_table = (AcpiHpetTable *)acpi_find_table("HPET", 0);
  if (!hpet_table) {
    kprintf("[HPET] Error: HPET ACPI table not found\n");
    return;
  }

  if (hpet_table->base_address.address_space != 0) {
    kprintf("[HPET] Error: HPET base address is not in System Memory\n");
    return;
  }

  uint64_t phys_addr = hpet_table->base_address.address;
  PageDirectory kernel_pml4 = vmm_get_kernel_pml4();

  uint64_t virt_addr = phys_addr + HHDM_BASE;
  if (!vmm_virt_to_phys(kernel_pml4, virt_addr)) {
    vmm_map_page(kernel_pml4, virt_addr, phys_addr, VMM_FLAG_PRESENT | VMM_FLAG_WRITABLE | VMM_FLAG_NO_CACHE);
  }

  g_hpet_base = (volatile uint8_t *)virt_addr;

  uint64_t caps = hpet_read_reg(HPET_REG_CAPABILITIES);
  g_period_fs = (uint32_t)(caps >> 32);
  g_is_64bit = (caps & HPET_CAP_64BIT) != 0;

  if (g_period_fs == 0 || g_period_fs > 100000000U) {
    kprintf("[HPET] Error: Invalid period: %u fs\n", g_period_fs);
    return;
  }

  g_frequency_hz = FEMTOSECONDS_PER_SECOND / (uint64_t)g_period_fs;

  uint64_t config = hpet_read_reg(HPET_REG_CONFIGURATION);
  config &= ~HPET_CFG_LEGACY_REPLACE;
  config |= HPET_CFG_ENABLE;
  hpet_write_reg(HPET_REG_CONFIGURATION, config);

  kprintf("[HPET] Initialized at %p: freq %u.%03u MHz (%s)\n",
          (void *)phys_addr,
          (uint32_t)(g_frequency_hz / 1000000ULL),
          (uint32_t)((g_frequency_hz % 1000000ULL) / 1000ULL),
          g_is_64bit ? "64-bit" : "32-bit"
  );
}

uint64_t hpet_read_counter(void) {
  if (!g_hpet_base) {
    return 0;
  }
  return hpet_read_reg(HPET_REG_MAIN_COUNTER);
}

uint64_t hpet_get_frequency(void) {
  return g_frequency_hz;
}

uint64_t hpet_get_nanos(void) {
  if (!g_period_fs) {
    return 0;
  }
  return (hpet_read_counter() * (uint64_t)g_period_fs) / FEMTOSECONDS_PER_NANO;
}

uint64_t hpet_get_millis(void) {
  if (!g_period_fs) {
    return 0;
  }
  return (hpet_read_counter() * (uint64_t)g_period_fs) / FEMTOSECONDS_PER_MILLI;
}

void hpet_sleep_us(uint64_t us) {
  if (!g_hpet_base || !g_period_fs || us == 0) {
    return;
  }

  uint64_t ticks_needed = (us * FEMTOSECONDS_PER_MICRO) / (uint64_t)g_period_fs;
  uint64_t start = hpet_read_counter();

  if (g_is_64bit) {
    while ((hpet_read_counter() - start) < ticks_needed) {
      __asm__ volatile("pause");
    }
  } else {
    while ((uint32_t)(hpet_read_counter() - start) < (uint32_t)ticks_needed) {
      __asm__ volatile("pause");
    }
  }
}

void hpet_sleep_ms(uint64_t ms) {
  if (!g_hpet_base || !g_period_fs || ms == 0) {
    return;
  }

  uint64_t ticks_needed = (ms * FEMTOSECONDS_PER_MILLI) / (uint64_t)g_period_fs;
  uint64_t start = hpet_read_counter();

  if (g_is_64bit) {
    while ((hpet_read_counter() - start) < ticks_needed) {
      __asm__ volatile("pause");
    }
  } else {
    while ((uint32_t)(hpet_read_counter() - start) < (uint32_t)ticks_needed) {
      __asm__ volatile("pause");
    }
  }
}
