#pragma once
#include <stdint.h>
#include <stddef.h>

#define GDT_ACCESS_ACCESSED (1 << 0)
#define GDT_ACCESS_RW (1 << 1)
#define GDT_ACCESS_DC (1 << 2)
#define GDT_ACCESS_EXEC (1 << 3)
#define GDT_ACCESS_SEGMENT (1 << 4)
#define GDT_ACCESS_RING0 (0 << 5)
#define GDT_ACCESS_RING3 (3 << 5)
#define GDT_ACCESS_PRESENT (1 << 7)

#define GDT_FLAG_64BIT (1 << 5)
#define GDT_FLAG_32BIT (1 << 6)
#define GDT_FLAG_GRANULARITY (1 << 7)

#define GDT_ACCESS_CODE_READABLE (GDT_ACCESS_PRESENT | GDT_ACCESS_SEGMENT | GDT_ACCESS_EXEC | GDT_ACCESS_RW)
#define GDT_ACCESS_DATA_WRITABLE (GDT_ACCESS_PRESENT | GDT_ACCESS_SEGMENT | GDT_ACCESS_RW)
#define GDT_ACCESS_TSS_64 (GDT_ACCESS_PRESENT | 0x09)

#define GDT_KERNEL_CODE_SELECTOR 0x08
#define GDT_KERNEL_DATA_SELECTOR 0x10
#define GDT_USER_DATA_SELECTOR 0x18
#define GDT_USER_CODE_SELECTOR 0x20
#define GDT_TSS_SELECTOR 0x28

typedef struct {
  uint16_t limit_low;
  uint16_t base_low;
  uint8_t base_mid;
  uint8_t access;
  uint8_t limit_flags;
  uint8_t base_high;
} __attribute__((packed)) GdtEntry;

typedef struct {
  uint32_t base_upper;
  uint32_t reserved;
} __attribute__((packed)) GdtTssHigh;

typedef struct {
  uint32_t reserved0;
  uint64_t rsp[3];
  uint64_t reserved1;
  uint64_t ist[7];
  uint64_t reserved2;
  uint16_t reserved3;
  uint16_t iomap_base;
} __attribute__((packed)) Tss;

typedef struct {
  uint16_t limit;
  uint64_t base;
} __attribute__((packed)) Gdtr;

void gdt_init(void);
void tss_set_stack(uint8_t ist_index, uint64_t stack_top);

extern void gdt_load(const Gdtr *gdtr, uint16_t code_sel, uint16_t data_sel);
extern void gdt_load_tss(uint16_t tss_sel);
