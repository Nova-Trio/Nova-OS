#include "gdt.h"

#define GDT_ENTRIES_COUNT 7

static GdtEntry g_gdt[GDT_ENTRIES_COUNT];
static Gdtr g_gdtr;
static Tss g_tss;

static void gdt_set_entry(size_t index, uint32_t base, uint32_t limit, uint8_t access, uint8_t flags) {
  g_gdt[index].limit_low = (uint16_t)(limit & 0xFFFF);
  g_gdt[index].base_low = (uint16_t)(base & 0xFFFF);
  g_gdt[index].base_mid = (uint8_t)((base >> 16) & 0xFF);
  g_gdt[index].access = access;
  g_gdt[index].limit_flags = (uint8_t)(((limit >> 16) & 0x0F) | (flags & 0xF0));
  g_gdt[index].base_high = (uint8_t)((base >> 24) & 0xFF);
}

static void gdt_set_tss(size_t index, uint64_t base, uint32_t limit) {
  gdt_set_entry(index, (uint32_t)base, limit, GDT_ACCESS_TSS_64 | GDT_ACCESS_RING0, 0);

  GdtTssHigh *tss_high = (GdtTssHigh *)&g_gdt[index + 1];
  tss_high->base_upper = (uint32_t)(base >> 32);
  tss_high->reserved = 0;
}

void tss_set_stack(uint8_t ist_index, uint64_t stack_top) {
  if (ist_index > 0 && ist_index <= 7) {
    g_tss.ist[ist_index - 1] = stack_top;
  }
}

void tss_set_rsp0(uint64_t stack_top){
  g_tss.rsp[0] = stack_top;
}

void gdt_init(void) {
  for (size_t i = 0; i < sizeof(Tss); i++) {
    ((uint8_t *)&g_tss)[i] = 0;
  }
  g_tss.iomap_base = sizeof(Tss);

  for (size_t i = 0; i < GDT_ENTRIES_COUNT; i++) {
    gdt_set_entry(i, 0, 0, 0, 0);
  }

  gdt_set_entry(1, 0, 0xFFFFF, GDT_ACCESS_CODE_READABLE | GDT_ACCESS_RING0, GDT_FLAG_64BIT | GDT_FLAG_GRANULARITY);
  gdt_set_entry(2, 0, 0xFFFFF, GDT_ACCESS_DATA_WRITABLE | GDT_ACCESS_RING0, GDT_FLAG_GRANULARITY);
  gdt_set_entry(3, 0, 0xFFFFF, GDT_ACCESS_DATA_WRITABLE | GDT_ACCESS_RING3, GDT_FLAG_GRANULARITY);
  gdt_set_entry(4, 0, 0xFFFFF, GDT_ACCESS_CODE_READABLE | GDT_ACCESS_RING3, GDT_FLAG_64BIT | GDT_FLAG_GRANULARITY);

  gdt_set_tss(5, (uint64_t)&g_tss, sizeof(Tss) - 1);

  g_gdtr.limit = sizeof(g_gdt) - 1;
  g_gdtr.base = (uint64_t)&g_gdt;

  gdt_load(&g_gdtr, GDT_KERNEL_CODE_SELECTOR, GDT_KERNEL_DATA_SELECTOR);
  gdt_load_tss(GDT_TSS_SELECTOR);
}
