#include "idt.h"
#include <gdt.h>
#include <console.h>


static IdtEntry g_idt[IDT_ENTRIES_COUNT];
static Idtr g_idtr;
static InterruptHandler g_interrupt_handlers[IDT_ENTRIES_COUNT];

static const char *g_exception_names[32] = {
  "#DE Divide By Zero",
  "#DB Debug",
  "#NMI Non-Maskable Interrupt",
  "#BP Breakpoint",
  "#OF Overflow",
  "#BR BOUND Range Exceeded",
  "#UD Invalid Opcode",
  "#NM Device Not Available",
  "#DF Double Fault",
  "Coprocessor Segment Overrun",
  "#TS Invalid TSS",
  "#NP Segment Not Present",
  "#SS Stack-Segment Fault",
  "#GP General Protection Fault",
  "#PF Page Fault",
  "Reserved",
  "#MF x87 FPU Floating-Point Error",
  "#AC Alignment Check",
  "#MC Machine Check",
  "#XM SIMD Floating-Point Exception",
  "#VE Virtualization Exception",
  "#CP Control Protection Exception",
  "Reserved", "Reserved", "Reserved", "Reserved", "Reserved", "Reserved",
  "#HV Hypervisor Injection Exception",
  "#VC VMM Communication Exception",
  "#SX Security Exception",
  "Reserved"
};

void idt_set_gate(uint8_t vector, void *isr, uint8_t type_attr, uint8_t ist) {
  uint64_t addr = (uint64_t)isr;
  g_idt[vector].offset_low = (uint16_t)(addr & 0xFFFF);
  g_idt[vector].selector = GDT_KERNEL_CODE_SELECTOR;
  g_idt[vector].ist = ist & 0x7;
  g_idt[vector].type_attr = type_attr;
  g_idt[vector].offset_mid = (uint16_t)((addr >> 16) & 0xFFFF);
  g_idt[vector].offset_high = (uint32_t)((addr >> 32) & 0xFFFFFFFF);
  g_idt[vector].reserved = 0;
}

void idt_register_handler(uint8_t vector, InterruptHandler handler) {
  g_interrupt_handlers[vector] = handler;
}

void idt_unregister_handler(uint8_t vector) {
  g_interrupt_handlers[vector] = NULL;
}

static void default_exception_handler(Registers *regs) {
  console_set_color(0x00FF4444, 0x00000000);

  const char *name = (regs->vector < 32) ? g_exception_names[regs->vector] : "Unknown Exception";
  kprintf("\n=== KERNEL PANIC ===\n");
  kprintf("Exception : %s (Vector %u)\n", name, (uint32_t)regs->vector);
  kprintf("Error Code: 0x%016llx\n", regs->error_code);
  kprintf("RIP       : 0x%016llx (CS: 0x%04x)\n", regs->rip, (uint32_t)regs->cs);
  kprintf("RSP       : 0x%016llx (SS: 0x%04x)\n", regs->rsp, (uint32_t)regs->ss);
  kprintf("RFLAGS    : 0x%016llx\n", regs->rflags);

  if (regs->vector == 14) {
    uint64_t cr2 = 0;
    __asm__ volatile("mov %%cr2, %0" : "=r"(cr2));
    kprintf("CR2       : 0x%016llx\n", cr2);
  }

  kprintf("\nRegisters:\n");
  kprintf("RAX: 0x%016llx  RBX: 0x%016llx  RCX: 0x%016llx\n", regs->rax, regs->rbx, regs->rcx);
  kprintf("RDX: 0x%016llx  RSI: 0x%016llx  RDI: 0x%016llx\n", regs->rdx, regs->rsi, regs->rdi);
  kprintf("RBP: 0x%016llx  R8 : 0x%016llx  R9 : 0x%016llx\n", regs->rbp, regs->r8, regs->r9);
  kprintf("R10: 0x%016llx  R11: 0x%016llx  R12: 0x%016llx\n", regs->r10, regs->r11, regs->r12);
  kprintf("R13: 0x%016llx  R14: 0x%016llx  R15: 0x%016llx\n", regs->r13, regs->r14, regs->r15);


  for (;;) {
    __asm__ volatile("cli; hlt");
  }
}

void interrupt_dispatch(Registers *regs) {
  if (regs->vector < IDT_ENTRIES_COUNT && g_interrupt_handlers[regs->vector]) {
    g_interrupt_handlers[regs->vector](regs);
    return;
  }

  if (regs->vector < 32) {
    default_exception_handler(regs);
  }
}

void idt_init(void) {
  for (size_t i = 0; i < IDT_ENTRIES_COUNT; i++) {
    idt_set_gate((uint8_t)i, isr_table[i], IDT_GATE_INTERRUPT, 0);
    g_interrupt_handlers[i] = NULL;
  }

  g_idtr.limit = sizeof(g_idt) - 1;
  g_idtr.base = (uint64_t)&g_idt;

  idt_load(&g_idtr);
}
