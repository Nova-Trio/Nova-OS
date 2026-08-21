#include "lapic.h"
#include "acpi.h"
#include "hpet.h"
#include <vmm.h>
#include <idt.h>
#include <console.h>

#define IA32_APIC_BASE_MSR 0x1B
#define IA32_APIC_BASE_MSR_ENABLE (1ULL << 11)
#define IA32_APIC_BASE_MSR_ADDR_MASK 0x000FFFFFFFFFF000ULL

#define LAPIC_REG_ID 0x020
#define LAPIC_REG_VERSION 0x030
#define LAPIC_REG_TPR 0x080
#define LAPIC_REG_EOI 0x0B0
#define LAPIC_REG_LDR 0x0D0
#define LAPIC_REG_DFR 0x0E0
#define LAPIC_REG_SVR 0x0F0
#define LAPIC_REG_ESR 0x280
#define LAPIC_REG_ICR_LOW 0x300
#define LAPIC_REG_ICR_HIGH 0x310
#define LAPIC_REG_LVT_TIMER 0x320
#define LAPIC_REG_LVT_THERMAL 0x330
#define LAPIC_REG_LVT_PERF 0x340
#define LAPIC_REG_LVT_LINT0 0x350
#define LAPIC_REG_LVT_LINT1 0x360
#define LAPIC_REG_LVT_ERROR 0x370
#define LAPIC_REG_TICR 0x380
#define LAPIC_REG_TCCR 0x390
#define LAPIC_REG_TDCR 0x3E0

#define LAPIC_SVR_ENABLE (1U << 8)
#define LAPIC_LVT_MASKED (1U << 16)
#define LAPIC_TIMER_PERIODIC (1U << 17)

static volatile uint8_t *g_lapic_base = NULL;
static uint64_t g_lapic_phys = 0;
static uint64_t g_ticks_per_ms = 0;

static inline uint64_t rdmsr(uint32_t msr) {
  uint32_t low, high;
  __asm__ volatile("rdmsr" : "=a"(low), "=d"(high) : "c"(msr));
  return ((uint64_t)high << 32) | low;
}

static inline void wrmsr(uint32_t msr, uint64_t val) {
  uint32_t low = (uint32_t)val;
  uint32_t high = (uint32_t)(val >> 32);
  __asm__ volatile("wrmsr" : : "a"(low), "d"(high), "c"(msr));
}

static inline uint32_t lapic_read(uint32_t reg) {
  return *(volatile uint32_t *)(g_lapic_base + reg);
}

static inline void lapic_write(uint32_t reg, uint32_t val) {
  *(volatile uint32_t *)(g_lapic_base + reg) = val;
}

static void spurious_handler(Registers *regs) {
  (void)regs;
}

static uint64_t discover_lapic_phys(void) {
  uint64_t phys_addr = 0;

  AcpiMadt *madt = (AcpiMadt *)acpi_find_table("APIC", 0);
  if (madt) {
    phys_addr = madt->lapic_address;

    uint8_t *curr = (uint8_t *)(madt + 1);
    uint8_t *end = (uint8_t *)madt + madt->header.length;

    while (curr < end) {
      AcpiMadtRecordHeader *rec = (AcpiMadtRecordHeader *)curr;
      if (rec->length == 0) {
        break;
      }
      if (rec->type == 5 && rec->length >= sizeof(AcpiMadtLapicAddressOverride)) {
        AcpiMadtLapicAddressOverride *override = (AcpiMadtLapicAddressOverride *)rec;
        phys_addr = override->phys_address;
        break;
      }
      curr += rec->length;
    }
  }

  uint64_t msr_val = rdmsr(IA32_APIC_BASE_MSR);
  uint64_t msr_phys = msr_val & IA32_APIC_BASE_MSR_ADDR_MASK;

  if (phys_addr == 0) {
    phys_addr = msr_phys;
  }

  if (phys_addr != msr_phys) {
    msr_val = (msr_val & ~IA32_APIC_BASE_MSR_ADDR_MASK) | (phys_addr & IA32_APIC_BASE_MSR_ADDR_MASK);
  }

  msr_val |= IA32_APIC_BASE_MSR_ENABLE;
  wrmsr(IA32_APIC_BASE_MSR, msr_val);

  return phys_addr;
}

void lapic_init(void) {
  g_lapic_phys = discover_lapic_phys();
  if (!g_lapic_phys) {
    kprintf("[LAPIC] Error: Unable to discover LAPIC physical address\n");
    return;
  }

  PageDirectory kernel_pml4 = vmm_get_kernel_pml4();
  uint64_t virt = g_lapic_phys + HHDM_BASE;

  if (!vmm_virt_to_phys(kernel_pml4, virt)) {
    vmm_map_page(kernel_pml4, virt, g_lapic_phys, VMM_FLAG_PRESENT | VMM_FLAG_WRITABLE | VMM_FLAG_NO_CACHE);
  }

  g_lapic_base = (volatile uint8_t *)virt;

  idt_register_handler(LAPIC_VECTOR_SPURIOUS, spurious_handler);

  lapic_write(LAPIC_REG_TPR, 0);
  lapic_write(LAPIC_REG_DFR, 0xFFFFFFFF);
  lapic_write(LAPIC_REG_LDR, (lapic_read(LAPIC_REG_LDR) & 0x00FFFFFF) | 1);
  lapic_write(LAPIC_REG_LVT_TIMER, LAPIC_LVT_MASKED);
  lapic_write(LAPIC_REG_LVT_PERF, LAPIC_LVT_MASKED);
  lapic_write(LAPIC_REG_LVT_LINT0, LAPIC_LVT_MASKED);
  lapic_write(LAPIC_REG_LVT_LINT1, LAPIC_LVT_MASKED);
  lapic_write(LAPIC_REG_LVT_ERROR, LAPIC_VECTOR_ERROR);
  lapic_write(LAPIC_REG_ESR, 0);
  lapic_write(LAPIC_REG_ESR, 0);
  lapic_write(LAPIC_REG_SVR, LAPIC_SVR_ENABLE | LAPIC_VECTOR_SPURIOUS);

  lapic_write(LAPIC_REG_TDCR, 0x03);
  lapic_write(LAPIC_REG_TICR, 0xFFFFFFFF);

  hpet_sleep_ms(10);

  uint32_t ticks_remaining = lapic_read(LAPIC_REG_TCCR);
  uint64_t elapsed_ticks = 0xFFFFFFFFU - ticks_remaining;
  g_ticks_per_ms = elapsed_ticks / 10;

  lapic_write(LAPIC_REG_TICR, 0);
  lapic_write(LAPIC_REG_LVT_TIMER, LAPIC_LVT_MASKED);

  kprintf("[LAPIC] Initialized at %p (ID %u, %llu ticks/ms)\n",
          (void *)g_lapic_phys, lapic_get_id(), g_ticks_per_ms
  );
}

void lapic_timer_start(uint32_t frequency_hz, uint8_t vector) {
  if (!g_lapic_base || !g_ticks_per_ms || frequency_hz == 0) {
    return;
  }

  uint64_t initial_count = (g_ticks_per_ms * 1000ULL) / frequency_hz;
  if (initial_count > 0xFFFFFFFFULL) {
    initial_count = 0xFFFFFFFFULL;
  }

  lapic_write(LAPIC_REG_TDCR, 0x03);
  lapic_write(LAPIC_REG_LVT_TIMER, LAPIC_TIMER_PERIODIC | vector);
  lapic_write(LAPIC_REG_TICR, (uint32_t)initial_count);
}

void lapic_timer_stop(void) {
  if (!g_lapic_base) {
    return;
  }
  lapic_write(LAPIC_REG_TICR, 0);
  lapic_write(LAPIC_REG_LVT_TIMER, LAPIC_LVT_MASKED);
}

void lapic_eoi(void) {
  if (g_lapic_base) {
    lapic_write(LAPIC_REG_EOI, 0);
  }
}

uint32_t lapic_get_id(void) {
  if (!g_lapic_base) {
    return 0;
  }
  return lapic_read(LAPIC_REG_ID) >> 24;
}

uint64_t lapic_get_ticks_per_ms(void) {
  return g_ticks_per_ms;
}
