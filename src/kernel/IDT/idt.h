#pragma once
#include <stdint.h>
#include <stddef.h>

#define IDT_ENTRIES_COUNT 256

#define IDT_GATE_INTERRUPT 0x8E
#define IDT_GATE_TRAP 0x8F
#define IDT_GATE_USER 0xEE

typedef struct {
  uint64_t r15, r14, r13, r12, r11, r10, r9, r8;
  uint64_t rbp, rdi, rsi, rdx, rcx, rbx, rax;
  uint64_t vector;
  uint64_t error_code;
  uint64_t rip;
  uint64_t cs;
  uint64_t rflags;
  uint64_t rsp;
  uint64_t ss;
} __attribute__((packed)) Registers;

typedef void (*InterruptHandler)(Registers *regs);

typedef struct {
  uint16_t offset_low;
  uint16_t selector;
  uint8_t ist;
  uint8_t type_attr;
  uint16_t offset_mid;
  uint32_t offset_high;
  uint32_t reserved;
} __attribute__((packed)) IdtEntry;

typedef struct {
  uint16_t limit;
  uint64_t base;
} __attribute__((packed)) Idtr;

void idt_init(void);
void idt_register_handler(uint8_t vector, InterruptHandler handler);
void idt_unregister_handler(uint8_t vector);
void idt_set_gate(uint8_t vector, void *isr, uint8_t type_attr, uint8_t ist);

extern void idt_load(const Idtr *idtr);
extern void *isr_table[IDT_ENTRIES_COUNT];
